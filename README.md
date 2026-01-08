## AVR128DB48 Poker (Texas Hold'em)

This project implements a complete two-player Texas Hold’em poker experience on an AVR128DB48 microcontroller. The system supports a continuous heads-up gameplay loop with automated deck initialization, card dealing, blind posting, and betting progression. Player hands are evaluated using a deterministic ranking and tie-break system to ensure correct outcomes during showdown. User input is handled through debounced button logic to prevent false triggers, while all visual output is rendered using a framebuffer-based OLED driver and a multi-module MAX7219 LED display. Together, these features enable reliable gameplay with clear user interaction and feedback.

## Hardware
<img width="1213" height="866" alt="image" src="https://github.com/user-attachments/assets/6fdef5dc-6017-49b6-8152-1da0353bc03d" />
This diagram illustrates the interaction between the AVR microcontroller and all external hardware components used in the poker system. The microcontroller serves as the central control unit, coordinating user input, game logic, display output, and audio/visual feedback.

User input is provided through push buttons (digital GPIO) and a potentiometer read via the ADC for bet sizing or menu navigation. Visual feedback is delivered through two display subsystems: an SSD1306 OLED connected over I²C for detailed game information (cards, balances, prompts), and a MAX7219-driven LED display connected over SPI for numeric or status output. Additional LEDs provide simple visual indicators controlled via GPIO.

Audio feedback is generated using a buzzer driven by the DAC or PWM output to signal game events such as betting actions, round transitions, or wins. The diagram highlights the communication protocols used (ADC, SPI, I²C, DAC) and shows how the embedded software maps game state changes to physical outputs in real time.

Together, this hardware–software integration enables an interactive, self-contained embedded poker game with clear separation between input handling, game logic, and output presentation.

## Software Logic
<img width="1532" height="785" alt="image" src="https://github.com/user-attachments/assets/e1160f88-3e97-4450-b2e3-b61f64a919ef" />
This section describes the embedded software structure and control flow for the poker game. The system is implemented as a deterministic state-based loop that manages hardware initialization, poker round execution, player interaction, hand evaluation, and game reset logic.

The software begins by initializing all peripherals (timers, RTC, OLED display, SPI devices, ADC, DAC, and GPIO). A poker round is then started by shuffling the deck, posting blinds, dealing cards, and resetting bets. Gameplay proceeds through multiple betting rounds (pre-flop, flop, turn, river), where players take actions via button inputs, with automatic check/call behavior enforced on timeouts.

Once betting concludes—either through all-in conditions or final betting actions—the game enters a showdown phase. Player hands are evaluated and scored, the winning hand is determined (or the pot is split in the event of a tie), and balances are updated accordingly. The software then checks for buy-in conditions: if both players retain balance, a new round begins; otherwise, the game declares a winner and resets the system state.

This structure ensures reliable gameplay progression, predictable timing behavior, and robust handling of edge cases such as ties, all-ins, and player timeouts.

## Repo Structure
---
### main.c
This file serves as the core of the application, integrating all hardware drivers with the poker game logic. It is responsible for initializing peripherals such as timers, ADC, GPIO, SPI, I²C, and audio outputs, and it implements the main game state machine that governs blinds, betting rounds, player actions, showdowns, and pot resolution. The file also contains the hand evaluation logic, including card sorting, rank classification, and tie-breaking comparisons. Display updates are coordinated through calls to the OLED and MAX7219 drivers, while optional UART output is used for debugging and gameplay logging.

---

### SPI.c and SPI.h
These files implement the SPI-based driver for the MAX7219 LED display modules. The interface provides initialization routines, display clearing functionality, and numeric rendering across multiple cascaded modules. Internally, the driver configures the AVR as an SPI master and broadcasts register updates to all connected MAX7219 devices simultaneously. A custom digit font is mapped across the 8×8 matrices, and update throttling is used to avoid unnecessary refresh operations. The driver assumes a fixed SPI pin configuration, which can be modified to match alternative hardware layouts.

---

### OLED.h and OLED.c
These files implement a complete SSD1306 OLED driver using the AVR’s TWI (I²C) peripheral. The driver maintains a full 1 KB framebuffer representing the 128×64 display and supports basic text rendering using a compact 6×8 font. High-level helper functions are provided to simplify common UI tasks, such as clearing the screen, positioning the cursor, printing strings, and rendering player-specific game information. Display updates are transmitted page-by-page to the OLED, ensuring consistent and flicker-free visual output during gameplay.

---

## Hardware Setup

### OLED (SSD1306, I2C)
The hardware configuration centers around an AVR development board acting as the system controller. Two SSD1306 OLED display is connected via I²C and is used to present each player's cards and balances (They should be hidden from the other person). A chain of four MAX7219-driven LED modules is connected over SPI to display numeric values such as the pot size or bet amounts. Player interaction is handled through active-low push buttons with internal pull-up resistors enabled, while a potentiometer connected to the ADC provides analog input for bet sizing or menu navigation. Additional LEDs and a buzzer provide visual and audio feedback for game events.

---

## Build & Flash

1. Open in **Microchip Studio** (or your AVR build system).
2. Ensure `F_CPU` is set to **16 MHz**.
3. Add all source files:
   - `main.c`, `SPI.c`, `OLED.c`
   - plus your UART implementation (`uart.c/.h`) if you’re using serial text I/O.
4. Build + flash to your board.
---

## How To Play

- Game starts with fixed **starting balance**, **small blind**, **big blind**
- Players receive hole cards
- Betting actions occur via buttons:
  - **CALL**, **FOLD**, **ALL-IN** (and any bet adjust logic if enabled)
- Community cards are dealt
- Showdown evaluates best hand and pays out:
  - winner takes pot
  - tie splits pot

OLED shows the current player’s cards + balance; MAX7219 shows key numbers (pot/balance).

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
