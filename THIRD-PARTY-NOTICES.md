# Third-Party Notices

The mod's own code is MIT licensed - see [LICENSE](LICENSE), copyright itsloopyo.
Everything listed below is a third-party or separately licensed component that is
compiled into the mod DLL or ships alongside it in the release ZIP, and its notice
is reproduced here as those licences require.

## MinHook

- **Version:** v1.3.4, the tagged release, commit
  `c3fcafdc10146beb5919319d0683e44e3c30d537`, vendored as a source copy in
  `extern/minhook/`. The copied files carry no version marker of their own - upstream
  keeps the number in the `CMakeLists.txt` that is not part of this copy - so the
  baseline is recorded here instead of being readable from the tree. Anyone checking
  should know that upstream's own `CMakeLists.txt` at that tag still declares patch
  version 3, so it computes `MINHOOK_VERSION` as `1.3.3`; that is upstream's, and the
  tag is v1.3.4.
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Installs the inline function hooks the mod uses to read the game camera, the scene view and the field of view accessor.
- **Bundled:** yes. Compiled into the mod DLL shipped in the release ZIP.
- **Modified:** yes, in `src/hook.c` only. `Initialize` allocates from the process heap
  (`GetProcessHeap`) instead of a private one (`HeapCreate`), and `Uninitialize` therefore
  does not call `HeapDestroy`. Every other vendored file is upstream's, unchanged, and
  `LICENSE.txt` and `AUTHORS.txt` are byte-identical to the tag. The licence permits
  modification; this is recorded so a reader diffing against upstream knows the
  difference is ours rather than a tampered copy. To check the whole claim rather than
  take it:

  ```bash
  git clone https://github.com/TsudaKageyu/minhook /tmp/minhook
  git -C /tmp/minhook checkout v1.3.4
  diff -r --strip-trailing-cr /tmp/minhook/src extern/minhook/src
  diff -r --strip-trailing-cr /tmp/minhook/include extern/minhook/include
  diff --strip-trailing-cr /tmp/minhook/LICENSE.txt extern/minhook/LICENSE.txt
  diff --strip-trailing-cr /tmp/minhook/AUTHORS.txt extern/minhook/AUTHORS.txt
  ```

  Only `src/hook.c` reports a difference, and only in the two hunks named above.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## cameraunlock-core

Git submodule at `cameraunlock-core/`, our own code in a separate repository under
its own MIT grant, which is why it carries a notice of its own rather than being
covered by this mod's `LICENSE`. Its C++ headers (`cameraunlock/...`) are compiled
into the mod DLL, and `install.cmd` / `uninstall.cmd` are thin wrappers that run
core's script bodies staged into the release ZIP under `shared/`. MIT requires its
notice to travel with those copies, so the full text is reproduced below.

- **Version:** commit `bd22895bb30ab7946d780b0af5782755e33e2cba`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Supplies the pose pipeline, camera and smoothing headers compiled into the mod DLL, and the install and uninstall script bodies the release ZIP's wrappers dispatch to.
- **Bundled:** yes. The headers are compiled into the mod DLL, and the script bodies ship in the release ZIP under `shared/`.

```
MIT License

Copyright (c) 2026 itsloopyo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Unreal Engine 3 conventions and names

- **Rights holder:** Epic Games, Inc. ("Unreal" and "Unreal Engine" are their
  trademarks.) BioShock Infinite is a product of Irrational Games and 2K Games.
- **License:** none granted or claimed. No Unreal Engine source, header or asset
  is copied, included or redistributed by this repository, in any form.
- **Usage:** the mod interoperates with a running copy of the game the player
  owns. Three engine conventions are stated in this mod's own source because
  code that talks to the engine cannot be written without them, and a reader has
  to be able to check them:

  - `FRotator` carries pitch, yaw and roll as three signed 32-bit integers with
    65536 units to the full turn (`src/ue3_types.h`).
  - The rotation matrix is row-vector, and composes pitch and roll before yaw
    (`src/ue3_rotation.h`).
  - The engine is left-handed with X forward, Y right and Z up, in centimetres
    (`src/head_pose.h`).

  Each is stated in this repository's own source at the file named beside it, and
  nowhere else.

  The mod calls the game's camera, HUD and Scaleform accessors and changes the
  position of its `XClikHUDCrosshair` widget. Object names, field layouts,
  function addresses and calling conventions come from the supported game
  executable. The crosshair's world, view and projection matrices are read at
  runtime to preserve its native depth and appearance. No engine or game source,
  headers or assets are included.

  Addresses for the supported executable are recorded in `src/build_profile.cpp`.
- **Bundled:** no.

---

## OpenTrack

- **Version:** n/a. The UDP pose protocol only, at no pinned version.
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** The mod receives head pose over OpenTrack's UDP protocol. No OpenTrack code is used or shipped.
- **Bundled:** no.

---
