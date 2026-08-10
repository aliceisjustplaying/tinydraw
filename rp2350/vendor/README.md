# Waveshare hardware sources

These files are the minimal display and touch subset of Waveshare's official
`RP2350-Touch-AMOLED-1.8` Pico SDK demo, downloaded from:

<https://files.waveshare.com/wiki/RP2350-Touch-AMOLED-1.8/RP2350-Touch-AMOLED-1.8.zip>

Source archive SHA-256:
`6c63c7184a9ce0f8c63874039a18dc5c9662e8a843a8e3c33252bcf04370a660`.
The imported files retain Waveshare's permissive license headers. The only
local driver change fixes the partial-refresh loop to send its final row.

Imported archive paths:

- `C/01-LCD/lib/Config/DEV_Config.{c,h}`
- `C/01-LCD/lib/QSPI_PIO/{qspi_pio.c,qspi_pio.h,qspi.pio}`
- `C/01-LCD/lib/AMOLED/AMOLED_1in8.{c,h}`
- `C/01-LCD/lib/Touch/FT3168.{c,h}`
