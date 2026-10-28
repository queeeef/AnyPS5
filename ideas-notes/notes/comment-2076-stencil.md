A case this PR should handle: a storage image at a stencil plane's address that is not the stencil plane.

In Palworld (PPSA23126), with this PR's `SeedStorageFromStencil` (unchanged at `f08fc2291`) merged locally, a 2536x1428 depth surface (`D32_SFLOAT_S8_UINT`) has its stencil plane at `0x3027630000`. Later in the frame the title places a 2536x1428 `A2B10G10R10` color target at that same address: Unreal's transient allocator reuses the memory. That image is 14.5 MB, the stencil plane 3.6 MB.

`cachedStorageTexture` calls `SeedStorageFromStencil` for any storage image whose base is a stencil plane's address, and `Transfer` then throws. This happened 36 times in one run:

```
storage image access to the stencil plane of depth surface 0x3027630000 (2536x1428, vk format 130) as a 2536x1428 image of vk format 64, dimension 1, 1 mips is not implemented
```

The color target stays non-resident (`[gpu] color target ... stays non-resident`), and textures over it keep the snapshot path.

Seeding only an image shaped like the plane (an R8 format, one slice, one mip, the surface's extent) and treating anything else as reused memory fixes it locally:

```diff
@@ void SeedStorageFromStencil(const Context& context, const std::shared_ptr<StorageTexture>& storage) {
     if (found == list.rend()) return;
     auto& surface = **found;
+    const auto& descriptor = storage->Descriptor();
+    const auto storageFormat = storage->StorageFormat();
+    const bool r8 = storageFormat == VK_FORMAT_R8_UINT || storageFormat == VK_FORMAT_R8_UNORM || storageFormat == VK_FORMAT_R8_SINT || storageFormat == VK_FORMAT_R8_SNORM;
+    const bool oneSlice = descriptor.dimension == TextureDimension::k2D || (descriptor.dimension == TextureDimension::k2DArray && descriptor.depthOrLastArray == 0);
+    if (!r8 || !oneSlice || descriptor.mipCount != 1 || descriptor.width != surface.target.extent.width || descriptor.height != surface.target.extent.height) return;
     if (surface.stencilSeeded == storage.get() && surface.stencilWriter.lock() == storage) return;
```

The condition is the one `Transfer` already checks for the stencil aspect, so a plane-shaped image is seeded exactly as before, and the depth and stencil tests still pass.

Limits of this report:
- The title hit this in one of eight runs, so the runs since the patch show no refusal, but they don't prove the patch is what removed it.
- I haven't written a test for the reuse case. If you prefer it as a commit on your branch, I can open a PR against it.

AI-assisted (Claude Code)
