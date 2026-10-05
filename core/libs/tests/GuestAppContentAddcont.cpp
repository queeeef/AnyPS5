#include "prx/libc/include/General.hpp"
#include "SceTypes.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

extern "C" int APS5_VABI sceAppContentAddcontMount(uint32_t service_label, const NpUnifiedEntitlementLabel* entitlement_label, AppContentMountPoint* mount_point);
extern "C" int APS5_VABI sceAppContentAddcontUnmount(const AppContentMountPoint* mount_point);

namespace {

constexpr int AppContentErrorParameter = static_cast<int>(0x80D90002);
constexpr int AppContentErrorNotFound = static_cast<int>(0x80D90005);

int failures = 0;

void Check(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "addcont check failed: %s\n", what.c_str());
    ++failures;
}

NpUnifiedEntitlementLabel EntitlementLabel(const char* text) {
    NpUnifiedEntitlementLabel label{};
    std::memcpy(label.data, text, std::strlen(text) + 1);
    return label;
}

int Mount(const char* text, AppContentMountPoint& point) {
    const auto label = EntitlementLabel(text);
    std::memset(point.data, 0xa5, sizeof(point.data));
    return sceAppContentAddcontMount(0, &label, &point);
}

AppContentMountPoint Point(const char* text) {
    AppContentMountPoint point{};
    std::memcpy(point.data, text, std::strlen(text) + 1);
    return point;
}

bool Padded(const AppContentMountPoint& point, const char* expected) {
    const auto length = std::strlen(expected);
    if (std::memcmp(point.data, expected, length) != 0) return false;
    for (std::size_t index = length; index < sizeof(point.data); ++index) {
        if (point.data[index] != 0) return false;
    }
    return true;
}

template <typename TException, typename TAction>
bool Throws(TAction action) {
    try {
        action();
    } catch (const TException&) {
        return true;
    }
    return false;
}

void Write(const std::filesystem::path& path, const char* text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

bool Resolves(const char* guest, const std::filesystem::path& host) {
    std::error_code error;
    return std::filesystem::equivalent(ResolvePath_nid_no_patch(guest), host, error) && !error;
}

}

int main() {
    const auto root = std::filesystem::temp_directory_path() / ("anyps5-addcont-" + std::to_string(std::random_device{}()));
    const auto work = root / "work";
    const auto first = work / "addcont" / "ABCDEFGHIJKLMNOP" / "first.txt";
    const auto second = work / "addcont" / "SECOND0000000001" / "second.txt";
    const auto third = work / "addcont" / "third" / "third.txt";
    const auto outside = root / "outside" / "secret.txt";
    Write(first, "first");
    Write(second, "second");
    Write(third, "third");
    Write(outside, "secret");
    std::filesystem::create_directories(work / "addcont" / "not.a.label");
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(work);

    AppContentMountPoint point{};
    const auto label = EntitlementLabel("ABCDEFGHIJKLMNOP");
    Check(sceAppContentAddcontMount(0, nullptr, &point) == AppContentErrorParameter, "a null entitlement label is rejected");
    Check(sceAppContentAddcontMount(0, &label, nullptr) == AppContentErrorParameter, "a null mount point is rejected");
    Check(sceAppContentAddcontUnmount(nullptr) == AppContentErrorParameter, "unmounting a null mount point is rejected");

    for (const char* missing : {"MISSING000000000", "", ".", "..", "../outside", "../../outside", "not.a.label", "a/b", "a\\b", "c:d"}) {
        Check(Mount(missing, point) == AppContentErrorNotFound, std::string("\"") + missing + "\" is not installed add-on content");
    }
    NpUnifiedEntitlementLabel unterminated{};
    std::memset(unterminated.data, 'A', sizeof(unterminated.data));
    Check(Throws<std::invalid_argument>([&] { sceAppContentAddcontMount(0, &unterminated, &point); }), "an unterminated entitlement label throws");

    Check(Mount("ABCDEFGHIJKLMNOP", point) == 0 && Padded(point, "/addcont0"), "the first add-on content is mounted at /addcont0");
    Check(Resolves("/addcont0/first.txt", first), "files under /addcont0 come from the add-on content directory");
    Check(Resolves("/addcont0", first.parent_path()), "/addcont0 itself is the add-on content directory");
    AppContentMountPoint secondPoint{};
    Check(Mount("SECOND0000000001", secondPoint) == 0 && Padded(secondPoint, "/addcont1"), "the second add-on content is mounted at /addcont1");
    Check(Resolves("/addcont1/second.txt", second), "files under /addcont1 come from the second directory");
    Check(Throws<std::logic_error>([&] { Mount("ABCDEFGHIJKLMNOP", point); }), "mounting mounted add-on content again throws");

    const auto mounted = Point("/addcont0");
    Check(sceAppContentAddcontUnmount(&mounted) == 0, "the first add-on content is unmounted");
    Check(!Resolves("/addcont0/first.txt", first), "/addcont0 no longer resolves to the add-on content after unmounting");
    Check(Throws<std::logic_error>([&] { sceAppContentAddcontUnmount(&mounted); }), "unmounting an unmounted point throws");
    const auto unknown = Point("/addcont7");
    Check(Throws<std::logic_error>([&] { sceAppContentAddcontUnmount(&unknown); }), "unmounting a point that was never mounted throws");
    const auto temporary = Point("/temp0");
    Check(Throws<std::logic_error>([&] { sceAppContentAddcontUnmount(&temporary); }), "unmounting a point of another kind throws");
    AppContentMountPoint unterminatedPoint{};
    std::memset(unterminatedPoint.data, 'a', sizeof(unterminatedPoint.data));
    Check(Throws<std::invalid_argument>([&] { sceAppContentAddcontUnmount(&unterminatedPoint); }), "an unterminated mount point throws");

    AppContentMountPoint thirdPoint{};
    Check(Mount("third", thirdPoint) == 0 && Padded(thirdPoint, "/addcont0"), "a freed mount point is reused");
    Check(Resolves("/addcont0/third.txt", third), "the reused mount point resolves to the new add-on content");
    Check(Resolves("/addcont1/second.txt", second), "the other mount point is unchanged");
    Check(Mount("ABCDEFGHIJKLMNOP", point) == 0 && Padded(point, "/addcont2"), "unmounted add-on content can be mounted again");
    Check(std::filesystem::exists(outside), "nothing outside the add-on content directory was touched");

    for (const auto* mountPoint : {&thirdPoint, &secondPoint, &point}) sceAppContentAddcontUnmount(mountPoint);
    std::filesystem::current_path(previous);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (failures != 0) return 1;
    std::printf("addcont mount tests passed\n");
    return 0;
}
