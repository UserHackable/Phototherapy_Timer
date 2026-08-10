# E760M single-panel therapy requirements (Gherkin)

Behavioral contracts derived from the SolRx **E-Series UVBNB User's Manual Rev 3.3A**
(May 2026) extract in this folder, scoped to the **household configuration in use**:

| Item | Value |
|------|--------|
| Model | **E760M-UVBNB** MASTER (Type M, home) |
| Bulbs | **6** Philips TL100W/01-FS72 |
| Panels | **Single MASTER only** — no ADD-ON devices, not MD66 / MD666+ |
| Nominal irradiance (Table 2, 1 device) | **6 mW/cm²** at ~10" from bulb plane (new bulbs, approx.) |
| Stock timer | Solarc standard MASTER timer (not clinic C01 firmware) |

**Not automated** via Cucumber yet — product / dosing contracts so custom
`session_timer` firmware and household procedure stay aligned with the manufacturer
manual. Physician instructions always override these scenarios.

## Features

| File | Scope |
|------|--------|
| [device_configuration.feature](device_configuration.feature) | Single E760M system identity and exclusions |
| [timer_stock_behavior.feature](timer_stock_behavior.feature) | Stock timer display, max time, beeps, power-fail, switchlock |
| [treatment_procedure.feature](treatment_procedure.feature) | Manual §14 treatment session procedure |
| [body_positions_single_panel.feature](body_positions_single_panel.feature) | Manual §12 for one panel (multi-position full coverage) |
| [therapy_psoriasis.feature](therapy_psoriasis.feature) | EGT psoriasis — single 6-bulb MASTER, skin types I–VI |
| [therapy_vitiligo.feature](therapy_vitiligo.feature) | EGT vitiligo — single 6-bulb MASTER |
| [therapy_atopic_dermatitis.feature](therapy_atopic_dermatitis.feature) | EGT atopic dermatitis (eczema) — single 6-bulb MASTER |
| [dose_adjustment_gaps.feature](dose_adjustment_gaps.feature) | Missed-treatment / gap rules from EGTs |
| [psoriasis_maintenance.feature](psoriasis_maintenance.feature) | Manual §15 long-term maintenance after clearing |

## Source chapters

- [13-exposure-guidelines.md](../13-exposure-guidelines.md)
- [14-treatment-procedure.md](../14-treatment-procedure.md)
- [12-body-positions.md](../12-body-positions.md)
- [15-psoriasis-long-term-maintenance-program.md](../15-psoriasis-long-term-maintenance-program.md)
- [20-troubleshooting.md](../20-troubleshooting.md) (Appendix D EGT tables)
- Original PDF: [../E760-E-Series-UVBNB-Users-Manual-Rev3.3A-May2026.pdf](../E760-E-Series-UVBNB-Users-Manual-Rev3.3A-May2026.pdf)

## Custom firmware note

Household `session_timer` may differ from stock Solarc timer UI (keypad, LCD,
network users). Timer *safety semantics* (countdown, end signal, retain last
time, hard max) and **all dosing times** in these features come from the manual.
