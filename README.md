# AVR Heads-Up Poker (OLED + MAX7219 + Buttons)

A 2-player Texas Hold’em style poker game written in C for an AVR microcontroller.  
The project drives:

- **OLED (SSD1306 over I2C/TWI)** for player cards + balance display
- **MAX7219 8x8 matrix modules** over SPI for numeric display (e.g., pot / balance / bet)
- **Buttons** (active-low w/ pullups) for in-game actions (call/fold/all-in, etc.)

> Built for `F_CPU = 16 MHz`.

---

## Features

- Heads-up (2 player) gameplay loop
- Deck init + dealing
- Hand evaluation (ranks + tie-break logic)
- Debounced button input task
- OLED text rendering via framebuffer
- SPI/MAX7219 multi-module digit display

---

## Diagram
<img width="1213" height="866" alt="image" src="https://github.com/user-attachments/assets/6fdef5dc-6017-49b6-8152-1da0353bc03d" />

## Software Logic
<img width="1532" height="785" alt="image" src="https://github.com/user-attachments/assets/e1160f88-3e97-4450-b2e3-b61f64a919ef" />



## Repo Structure

### `main.c`
**Core game + hardware orchestration**
- Initializes peripherals (clock/timers, ADC, buttons, buzzer/LED, etc.)
- Implements the poker game state machine (blinds, betting actions, showdown, pot split)
- Contains hand scoring + helper logic (sorting, rank conversion, etc.)
- Uses:
  - `OLED_ShowPlayer()` to show each player’s hole cards + balance
  - `Matrix_DisplayNumber()` to show numeric values on MAX7219 modules
  - UART (via `uart.h`) for text prompts/logging (**not included in current upload**)

**Notable internal modules inside `main.c`:**
- Button debouncing state struct + task
- Deck functions (`init_deck`, `deal_card`, etc.)
- Hand scoring (`handscore_t`, ranking names, tie-break arrays)

---

### `SPI.h`
Public interface for the MAX7219 display driver.

**Defines**
- `NUM_MODULES 4` (4 cascaded MAX7219 modules)

**Functions**
- `SPI_Init()` – configures SPI + initializes MAX7219 registers
- `Matrix_Clear()` – clears all rows on all modules
- `Matrix_DisplayNumber(uint16_t value)` – renders up to 4 digits across modules

---

### `SPI.c`
**MAX7219 driver over hardware SPI**
- Sets up SPI0 as master
- Implements:
  - Byte send + register broadcast to all cascaded modules
  - A digit font (8 rows per digit) rendered into MAX7219 rows
- Includes a small “deadband”/update guard so the display isn’t constantly rewritten.

**Wiring assumptions (typical)**
- Uses `PORTA` pins for SPI and MAX7219 load/latch (CS).
- `LOAD_LOW()/LOAD_HIGH()` toggles `PORTA.PIN7` (chip select / LOAD).

> If your board uses different pins, update the `PORTA.DIRSET` / `PINx_bm` usage in `SPI_Init()` and the `LOAD_*` macros.

---

### `OLED.h`
Public interface for the OLED driver.

**Constants**
- `OLED_WIDTH 128`, `OLED_HEIGHT 64`, `OLED_ADDR 0x3C`
- `OLED_Bus { BUS_TWI0, BUS_TWI1 }` to select between TWI peripherals

**Functions**
- `OLED_init(bus)`
- `OLED_clear(bus)`
- `OLED_update(bus)`
- `OLED_setCursor(col,row)` – cursor in *text cells* (6x8 font)
- `OLED_print(str)`
- `OLED_ShowPlayer(bus, player, card1, card2, balance)` – convenience UI screen

---

### `OLED.c`
**SSD1306 framebuffer driver over TWI (I2C)**
- Initializes TWI as master w/ internal pullups
- Maintains a full framebuffer (`128 x 64 / 8 = 1024 bytes`)
- Implements a compact **6x8** font for:
  - space, digits `0–9`, letters `A–Z`, `:`
- `OLED_update()` pushes framebuffer page-by-page to the display
- `OLED_ShowPlayer()` clears, prints player line + balance line, then updates display

---

## Hardware Setup (Typical)

### OLED (SSD1306, I2C)
- VCC → 3.3V or 5V (depending on your module)
- GND → GND
- SDA/SCL → AVR TWI pins used by `TWI0` or `TWI1`
- Default I2C address in code: `0x3C`

### MAX7219 modules (4 cascaded)
- DIN / CLK / CS(LOAD) connected to AVR SPI + a GPIO for LOAD
- Confirm `NUM_MODULES` matches your chain count (default = 4)

### Buttons
- Active-low with internal pullups enabled
- Connected to `PORTB` pins (see button defines near top of `main.c`)

---

## Build & Flash

1. Open in **Microchip Studio** (or your AVR build system).
2. Ensure `F_CPU` is set to **16 MHz**.
3. Add all source files:
   - `main.c`, `SPI.c`, `OLED.c`
   - plus your UART implementation (`uart.c/.h`) if you’re using serial text I/O.
4. Build + flash to your board.

> If your project uses a different AVR part than the one this was written for, you may need to adjust peripheral register names (SPI0/TWI0 naming varies by family).

---

## How To Play (High Level)

- Game starts with fixed **starting balance**, **small blind**, **big blind**
- Players receive hole cards
- Betting actions occur via buttons:
  - **CALL**, **FOLD**, **ALL-IN** (and any bet adjust logic if enabled)
- Community cards are dealt
- Showdown evaluates best hand and pays out:
  - winner takes pot
  - tie splits pot

OLED shows the current player’s cards + balance; MAX7219 shows key numbers (pot/bet/balance depending on your UI flow).

---

## Example Gameplay Output (UART Console)

Example of an end-of-hand showdown:

```text
Community Cards 1, 2, 3, 4, 5: 5S 10D 9H QS KS
Player 1 cards: 8C QH
Player 2 cards: 7D AD

Showdown. Pot = 20

It's a tie! Pot is split.
Player 1 money: 1000
Player 2 money: 1000
Press any key to return to menu
