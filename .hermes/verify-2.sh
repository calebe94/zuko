#!/usr/bin/env bash
# Verification pass 2:
#   A. real build of /src (the actual project, not a copy)
#   B. FPU proof: look for single-precision VFP instructions in the linked ELF
#   C. CMSIS-DSP link proof: build a copy that actually CALLS arm_fir_f32
set -u

cd /src

echo "############ A. REAL BUILD ############"
pio run -e rpipico2 2>&1 | tail -12

echo
echo "############ B. FPU PROOF (linked ELF) ############"
TC=$(ls -d /root/.platformio/packages/toolchain-rp2040-earlephilhower 2>/dev/null || true)
echo "toolchain dir: ${TC:-NOT PRESENT}"
OBJDUMP=$TC/bin/arm-none-eabi-objdump
ELF=/src/.pio/build/rpipico2/firmware.elf

echo "--- ldd-ish: hardware FP instructions present in the firmware ---"
for op in vfma.f32 vmul.f32 vadd.f32 vmov.f32 vldr vdiv.f32; do
    n=$("$OBJDUMP" -d "$ELF" 2>/dev/null | grep -c -w "$op")
    echo "  $op : $n occurrences"
done

echo "--- the waveshaper / FIR functions and their float instructions ---"
"$OBJDUMP" -d "$ELF" 2>/dev/null | awk '
  /<.*(waveshape|firAt|printDspSanity).*>:/ { infn=1; print; next }
  infn && /^$/ { infn=0 }
  infn && /(vfma|vmul|vadd|vsub|vdiv|vldr|vstr)\.[fs]32/ { print "     " $0 }
' | head -40

echo "--- cycle-countish sanity: does the ELF reference __aeabi_fmul (soft float)? ---"
if "$OBJDUMP" -d "$ELF" 2>/dev/null | grep -q "__aeabi_fmul"; then
    echo "  WARNING: __aeabi_fmul found -> some float multiply is done in software"
else
    echo "  ok: no __aeabi_fmul in the disassembly"
fi

echo
echo "############ C. CMSIS-DSP LINK PROOF ############"
rm -rf /tmp/cmsis && mkdir -p /tmp/cmsis && cp -r /src/. /tmp/cmsis/ 2>/dev/null
rm -rf /tmp/cmsis/.pio /tmp/cmsis/.git

cat > /tmp/cmsis/src/cmsis_link_probe.cpp <<'EOF'
// Throwaway link probe: does the ArduCMSIS-DSP library actually link, and are
// the arm_fir_*_f32 symbols callable with the expected signature?
#include <Arduino.h>
#include "arm_math.h"

static arm_fir_instance_f32 s_fir;
static float s_state[8 + 3];
static float s_coeffs[8];

void cmsisLinkProbeSetup() {
    for (int i = 0; i < 8; ++i) s_coeffs[i] = 0.125f;
    arm_fir_init_f32(&s_fir, 8, s_coeffs, s_state, 4);
    float in[4] = {1.0f, 0.5f, -0.5f, -1.0f};
    float out[4] = {0};
    arm_fir_f32(&s_fir, in, out, 4);
    Serial.printf("cmsis probe: out[0]=%.4f\n", out[0]);
}
EOF

python3 - <<'PY'
p = "/tmp/cmsis/src/main.cpp"
s = open(p).read()
s = s.replace("void setup() {", "extern void cmsisLinkProbeSetup();\n\nvoid setup() {\n    cmsisLinkProbeSetup();", 1)
open(p, "w").write(s)
PY

cd /tmp/cmsis && pio run -e rpipico2 2>&1 | tail -18
