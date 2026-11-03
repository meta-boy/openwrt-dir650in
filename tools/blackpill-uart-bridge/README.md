# Black Pill USB-serial bridge

Bare-metal USB CDC-ACM <-> USART1 bridge for an STM32F401 "Black Pill" (25 MHz HSE), written
because the board's previous Arduino sketch ran its UART at a fixed 115200 baud. Here the UART
follows the baud rate the host sets (SET_LINE_CODING), 8N1. No libraries, ~2.6 KB.

| Pin | Function |
|---|---|
| PA9 | TX -> router RX |
| PA10 | RX <- router TX |
| GND | GND |

Build and flash (DFU: hold BOOT0, tap NRST, release BOOT0):

```sh
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os \
      -ffreestanding -fno-builtin -nostdlib -c main.c -o main.o
ld.lld -T link.ld main.o -o bridge.elf && llvm-objcopy -O binary bridge.elf bridge.bin
dfu-util -a 0 -s 0x08000000:leave -D bridge.bin
```

Back up the existing firmware first: `dfu-util -a 0 -s 0x08000000:524288 -U backup.bin`.
