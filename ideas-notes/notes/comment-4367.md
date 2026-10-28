Tested on Windows at this head (`5573e3238`).

- **Setup:** Windows 11 Pro 26300, MinGW-w64 GCC 15.2.0 (WinLibs r7), Release, `BUILD_TESTING=ON`.
- **`guest_memory`:** passes, including `CheckAliasAcrossDirectAllocations`, `CheckAliasOfReorderedSharedPages` and `CheckAliasRefusesPagesThatAreNotSharedViews`.
- **Without the fix:** with `WindowsMappings.hpp` restored to the base and the rest of this PR kept, `guest_memory` aborts with `read-write alias of shared guest memory 0x200d60000+0xc000: the range is not one contiguous run of views of a section`. That is the error behind cause 2 in #4260.

I reviewed the change against a local equivalent I wrote for #4260 (one placeholder split into one view per run of section offsets). The two do the same thing, so I'm dropping mine in favour of this PR. In Palworld (PPSA23126) the local version made the 43 spans that failed at the alias import, and the CPU copies of the regions under them went from 2–56 GB per 10 s to 0. I'll run this PR together with oneandonlydean's `up4/host-import-bind-join` on that scene and report on #4260.

AI-assisted (Claude Code)
