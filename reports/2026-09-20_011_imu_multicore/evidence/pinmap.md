# Existing signal harness (not a new circuit)

Source: AquaBeacon/pcb/PARENT_MAIN/部品一覧.md lines 79-80. Component-side top view. Actual continuity is unmeasured. Supply rails are outside this signal-only model. Structural ERC is not electrical validation.

=== ピン配置表 ===
Sony (Extension signals):
    D02        -> PPS_SOURCE
    D01        -> UART_SOURCE
    GND        -> GND
J_SPR1 (XH 3P component-side left to right):
    1          -> PPS_SOURCE
    2          -> UART_SOURCE
    3          -> GND
R_PPS (100 ohm):
    1          -> PPS_SOURCE
    2          -> PPS_INPUT
R_EVT (100 ohm):
    1          -> UART_SOURCE
    2          -> UART_INPUT
Pico (Pico 2 W):
    GP6        -> PPS_INPUT
    GP7        -> UART_INPUT
    GND        -> GND

=== ネット接続表 ===
GND [GND]: J_SPR1.3, Pico.GND, Sony.GND
PPS_INPUT: Pico.GP6, R_PPS.2
PPS_SOURCE: J_SPR1.1, R_PPS.1, Sony.D02
UART_INPUT: Pico.GP7, R_EVT.2
UART_SOURCE: J_SPR1.2, R_EVT.1, Sony.D01