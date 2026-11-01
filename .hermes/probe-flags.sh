#!/usr/bin/env bash
# Root-cause probe for the bits/c++config.h failure, plus a build of a throwaway
# copy with the float flags stripped. Read-only with respect to /src.
set -u

TC=/root/.platformio/packages/toolchain-rp2040-earlephilhower

echo "=== host arch ==="
uname -m

echo "=== platformio packages present in the image ==="
ls /root/.platformio/packages/

echo "=== c++config.h shipped by the toolchain ==="
find "$TC/arm-none-eabi/include/c++" -name c++config.h 2>/dev/null

echo "=== g++ version ==="
"$TC/bin/arm-none-eabi-g++" --version | head -1

echo "=== include search WITH -mfloat-abi=hard -mfpu=fpv5-sp-d16 (the failing build) ==="
"$TC/bin/arm-none-eabi-g++" -mcpu=cortex-m33 -mfloat-abi=hard -mfpu=fpv5-sp-d16 -mthumb \
    -x c++ -E -v /dev/null 2>&1 | sed -n '/#include <...> search starts here/,/End of search list/p'

echo "=== include search with core defaults (armv8-m.main+fp+dsp, softfp) ==="
"$TC/bin/arm-none-eabi-g++" -mcpu=cortex-m33 -march=armv8-m.main+fp+dsp -mfloat-abi=softfp -mthumb \
    -x c++ -E -v /dev/null 2>&1 | sed -n '/#include <...> search starts here/,/End of search list/p'

echo "=== probe build: copy of the project with the two float flags removed ==="
rm -rf /tmp/probe && mkdir -p /tmp/probe && cp -r /src/. /tmp/probe/ 2>/dev/null
rm -rf /tmp/probe/.pio /tmp/probe/.git

python3 - <<'PY'
p = "/tmp/probe/platformio.ini"
s = open(p).read()
for flag in ("    -mfloat-abi=hard\n", "    -mfpu=fpv5-sp-d16\n"):
    s = s.replace(flag, "")
open(p, "w").write(s)
print("build_flags now:")
for line in open(p):
    st = line.strip()
    if st.startswith("-") and not st.startswith(";"):
        print("   " + st)
PY

cd /tmp/probe && pio run -e rpipico2 2>&1 | tail -30
