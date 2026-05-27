# cm_ethernet

F28388D **Cortex-M4 (CM)** project: lwIP UDP transmitter, consumer of
the CPU1 ADC ring.

Based on the canonical C2000Ware example
`libraries/communications/Ethernet/third_party/lwip/examples/enet_lwip_udp/cm/`.
The lwIP support files (`lwiplib.c`, `f2838xif.c`, `enet.c`,
`device_cm.c`, `pinout.c`, `lwipopts.h`, `lwippools.h`,
`startup_ccs.c`, `2838x_flash_lnk_cm_lwip.cmd`) are pulled in
unmodified from C2000Ware via the projectspec.

* Static IP `192.168.10.10/24`, gateway `192.168.10.1` (unused)
* MAC `A8:63:F2:00:39:02` (TI OUI, factory-printed on the controlCARD)
* UDP socket bound to source port 5006, sends to `192.168.10.20:5005`
* SysTick = 1 ms; CM polls `g_shared->producer_seq` each tick, packs
  50 samples per packet, ships 200 pkt/s
* CM **never** writes into `MSGRAM_CPU_TO_CM` (would raise an
  IMPRECISERR bus fault). The consumer index is the CM-local
  `s_consumer_seq` static.

**Toolchain:** TI ARM CGT 20.2.x LTS (NOT TI Clang - C2000Ware's lwIP
port is built and linked against the older ARM CGT). `--float_support=none`.

**Active build configuration:** `CM_FLASH` only. To flash, follow the
strict order documented in the [repo README](../README.md):
load CPU1 -> run CPU1 -> pause -> load CM.

**Warnings:** the build emits five `#179-D` (unused variable) warnings
in the C2000Ware lwIP source files (`enet.c`, `f2838xif.c`). The
projectspec adds `--diag_suppress=179` so a fresh import is clean;
existing imports can either re-import or add the flag manually under
`Project -> Properties -> Build -> ARM Compiler -> Advanced Options
-> Diagnostic Options`.

---

## Author

**Masoud Bakhshi**

* Website  : <https://www.plan22.net>
* Email    : <info@plan22.net>
* LinkedIn : <https://www.linkedin.com/in/masoud-bakhshi-78490846/>

MIT with required attribution. See [../LICENSE](../LICENSE).
