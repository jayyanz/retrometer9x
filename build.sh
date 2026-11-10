#!/bin/sh
# Cross-builds the Win9x DLLs with mingw-w64 (i686). No C runtime is linked:
# only KERNEL32, USER32 and GDI32 imports that exist on Windows 95/98.
set -e
cd "$(dirname "$0")"
CC=${CC:-i686-w64-mingw32-gcc}
OUT=out
mkdir -p $OUT build

CFLAGS="-O2 -march=i586 -mtune=pentium2 -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables \
 -mno-stack-arg-probe -fno-builtin -Wall -Wno-unused-function -DWIN32_LEAN_AND_MEAN -DWINVER=0x0400 -D_WIN32_WINNT=0x0400"
LDOS="-Wl,--subsystem,windows:4.0 -Wl,--major-os-version,4 -Wl,--minor-os-version,0 \
 -Wl,--disable-dynamicbase -Wl,--disable-nxcompat -Wl,--enable-stdcall-fixup -Wl,--file-alignment,512"
LDDLL="-shared -nostdlib $LDOS -Wl,-e,_DllMain@12"
LIBS="-lkernel32 -luser32 -lgdi32 -lgcc"

dll() {  # name exports-list source...
  name=$1; list=$2; shift 2
  python3 gen/gen_stubs.py exports/$list.txt $name.dll build/$name
  $CC $CFLAGS -s -o $OUT/$name.dll "$@" common/fpscore.c common/fpssettings.c build/$name/exptab.c build/$name/stubs.S \
      build/$name/exports.def $LDDLL $LIBS
  echo "built $OUT/$name.dll"
}

dll opengl32 opengl32 opengl/gl_proxy.c
dll glide2x  glide2x  glide/glide_proxy.c
dll glide3x  glide3x  glide/glide_proxy.c -DGLIDE3
dll d3d8     d3d8     d3d8/d3d8_proxy.c
dll fpsdd    ddraw    ddraw/ddraw_proxy.c

$CC $CFLAGS -s -o $OUT/rmpatch.exe patcher/rmpatch.c common/pepatch.c -nostdlib $LDOS -Wl,-e,_entry@0 $LIBS
echo "built $OUT/rmpatch.exe"

${WINDRES:-i686-w64-mingw32-windres} -I config config/retrometer.rc -O coff -o build/retrometer.res
$CC $CFLAGS -s -o $OUT/retrometer.exe config/retrometer.c config/gamedb.c common/fpssettings.c common/pepatch.c build/retrometer.res \
    -nostdlib $LDOS -Wl,-e,_entry@0 $LIBS -lcomdlg32 -lshell32
echo "built $OUT/retrometer.exe"
cp dist-README.txt $OUT/README.txt
cp LICENSE $OUT/LICENSE.txt
