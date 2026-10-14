#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Diagnostics.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

namespace AgcDriver::DriverDetail {

std::uint32_t Driver::drawUserWord(const DrawProgram& program, std::int32_t sgpr) {
    require(sgpr >= 0 && static_cast<std::uint32_t>(sgpr) >= program.firstUserSgpr, "invalid draw offset SGPR");
    const auto index = static_cast<std::uint32_t>(sgpr) - program.firstUserSgpr;
    require(index < program.userData.size(), "draw offset SGPR exceeds user data");
    return program.userData[index];
}

void Driver::FoldDrawOffsets(const ShaderRecompiler::RecompileResult& result, const DrawProgram& program, Pm4::DrawParameters& parameters) {
    if (result.vertexOffsetSgpr >= 0) {
        const auto offset = drawUserWord(program, result.vertexOffsetSgpr);
        require(offset <= std::numeric_limits<std::uint32_t>::max() - parameters.firstVertex, "draw vertex offset overflow");
        parameters.firstVertex += offset;
    }
    if (result.instanceOffsetSgpr >= 0) parameters.firstInstance = drawUserWord(program, result.instanceOffsetSgpr);
}

namespace {

void NoteNonVertexIndirect(const std::string& reason) {
    static std::mutex mutex;
    static std::map<std::string, std::uint64_t> counts;
    static auto window = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex);
    ++counts[reason];
    const auto now = std::chrono::steady_clock::now();
    if (now - window < std::chrono::seconds(10)) return;
    std::string line = "[local-indirect-nonvertex] 10 s:";
    for (const auto& [name, count] : counts) line += " " + name + " " + std::to_string(count) + ";";
    std::fprintf(stderr, "%s\n", line.c_str());
    counts.clear();
    window = now;
}

}

std::optional<Graphics::IndirectDrawPath> Driver::ClassifyIndirectDraw(const ShaderRecompiler::RecompileResult& result, const Graphics::State& graphics, const DrawProgram& frontProgram, const std::shared_ptr<VulkanDevice>& localDevice, Pm4::DrawParameters& drawParameters, bool traceIndirect) {
    std::optional<Graphics::IndirectDrawPath> indirectCpu;
    auto& indirect = *drawParameters.indirect;
    using Rule = Pm4::DrawParameters::IndirectDraw::Rule;
    using Path = Graphics::IndirectDrawPath;
    const auto support = localDevice->DrawIndirectSupport();
    const auto fail = [&](Path reason) { if (!indirectCpu) indirectCpu = reason; };
    constexpr std::uint32_t NoLocation = 0x280u;
    const bool meshPath = graphics.stages.mesh.has_value() && !graphics.stages.tessellation && !graphics.rectList;
    const bool meshGpuSide = meshPath && drawParameters.indexed && indirect.count == 1 && !indirect.countIndirect && indirect.baseVertexLocation == NoLocation && indirect.startInstanceLocation == NoLocation && (!indirect.drawIndexEnabled || indirect.drawIndexLocation == NoLocation);
    if ((graphics.stages.path != Graphics::ShaderPath::Vertex || (graphics.rectList && graphics.stages.mesh.has_value())) && !meshGpuSide) {
        fail(Path::NonVertexPath);
        static const bool profile = std::getenv("APS5_PROFILE_DRAW") != nullptr;
        if (profile) {
            const char* paths[] = {"vertex", "geometry", "tessellation", "tessellation+geometry"};
            std::string reason = graphics.rectList ? "rect list" : std::string(paths[static_cast<int>(graphics.stages.path)]) + (graphics.stages.mesh.has_value() ? " (mesh stages" + std::string(graphics.stages.tessellation ? ", tessellated)" : ")") : "");
            if (meshPath) {
                if (!drawParameters.indexed) reason += ": mesh not indexed";
                else if (indirect.count != 1) reason += ": mesh count " + std::string(indirect.count > 1 ? ">1" : "0");
                else if (indirect.countIndirect) reason += ": mesh count from memory";
                else if (indirect.baseVertexLocation != NoLocation) reason += ": mesh base-vertex location";
                else if (indirect.startInstanceLocation != NoLocation) reason += ": mesh start-instance location";
                else reason += ": mesh draw-index location";
            }
            reason += indirect.opcode == 0 ? "" : " op 0x" + [&] { char text[8]; std::snprintf(text, sizeof(text), "%x", indirect.opcode); return std::string(text); }();
            NoteNonVertexIndirect(reason);
        }
    }
    if (indirect.drawIndexEnabled && indirect.drawIndexSgpr >= 0) fail(Path::DrawIndex);
    if (indirect.countIndirect && !support.count) fail(Path::FeatureGap);
    if (indirect.countIndirect && indirect.count > 1 && !support.multi) fail(Path::FeatureGap);
    const auto dimension = [&](std::int32_t k, std::int32_t p, bool shared, bool conflict, std::uint32_t x, Rule& rule, std::uint32_t& constant) {
        if (p < 0) {
            rule = Rule::Constant;
            const auto offset = k >= 0 ? drawUserWord(frontProgram, k) : 0u;
            require(offset <= std::numeric_limits<std::uint32_t>::max() - x, "draw vertex offset overflow");
            constant = x + offset;
        } else if (conflict) fail(Path::FetchUnknown);
        else if (k != p || shared) fail(Path::NotFolded);
        else if (x != 0) fail(Path::IndxOffset);
        else rule = Rule::InPlace;
    };
    dimension(result.vertexOffsetSgpr, indirect.baseVertexSgpr, result.vertexOffsetShared, result.vertexOffsetConflict, drawParameters.indexed ? 0u : indirect.indxOffset, indirect.vertexRule, indirect.vertexConstant);
    dimension(result.instanceOffsetSgpr, indirect.startInstanceSgpr, result.instanceOffsetShared, result.instanceOffsetConflict, 0u, indirect.instanceRule, indirect.instanceConstant);
    if (!support.firstInstance && (indirect.instanceRule == Rule::InPlace || indirect.instanceConstant != 0)) fail(Path::FeatureGap);
    if ((indirect.vertexRule == Rule::Constant || indirect.instanceRule == Rule::Constant) && indirect.count > 256) fail(Path::FeatureGap);

    static const std::uint64_t vertexCap = [] { const char* text = std::getenv("APS5_INDIRECT_VERTEX_MIB"); return (text ? std::strtoull(text, nullptr, 10) : 64ull) << 20u; }();
    for (const auto& attribute : result.vertexAttributes) {
        const auto stride = (attribute.resource.fields[1] >> 16u) & 0x3fffu;
        const auto extent = stride == 0 ? static_cast<std::uint64_t>(attribute.resource.fields[2]) : static_cast<std::uint64_t>(attribute.resource.fields[2]) * stride;
        if (extent > vertexCap) fail(Path::VertexRange);
    }
    if (traceIndirect) {
        const auto location = [](std::uint32_t value, std::int32_t sgpr) { char text[24]; if (value == 0x280u) std::snprintf(text, sizeof(text), "none"); else std::snprintf(text, sizeof(text), "0x%x(s%d)", value, sgpr); return std::string(text); };
        const auto rule = [](Rule value, std::uint32_t constant) { char text[24]; if (value == Rule::InPlace) std::snprintf(text, sizeof(text), "in-place"); else std::snprintf(text, sizeof(text), "const %u", constant); return std::string(text); };
        std::string decision = indirectCpu ? "cpu (" + std::string(Graphics::IndirectDrawPathName(*indirectCpu)) + ")" : "gpu vertex=" + rule(indirect.vertexRule, indirect.vertexConstant) + " instance=" + rule(indirect.instanceRule, indirect.instanceConstant);
        std::fprintf(stderr, "[draw] indirect 0x%x args 0x%llx stride %u count %u%s locs base=%s inst=%s idx=%s%s analyzer v=%d i=%d shared=%d/%d conflict=%d/%d indx=%u -> %s\n", indirect.opcode, static_cast<unsigned long long>(indirect.arguments), indirect.stride, indirect.count, indirect.countIndirect ? " (indirect)" : "", location(indirect.baseVertexLocation, indirect.baseVertexSgpr).c_str(), location(indirect.startInstanceLocation, indirect.startInstanceSgpr).c_str(), location(indirect.drawIndexLocation, indirect.drawIndexSgpr).c_str(), indirect.drawIndexEnabled ? " (enabled)" : "", result.vertexOffsetSgpr, result.instanceOffsetSgpr, result.vertexOffsetShared ? 1 : 0, result.instanceOffsetShared ? 1 : 0, result.vertexOffsetConflict ? 1 : 0, result.instanceOffsetConflict ? 1 : 0, indirect.indxOffset, decision.c_str());
    }
    return indirectCpu;
}

}
