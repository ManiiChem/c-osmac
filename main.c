#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <raylib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

///////////////////////////
//        CONFIG         //

const char* ROM_NAME = "space_invaders_david_winter.ch8"; // TBA
bool LEGACY_SHIFT = false; // false for modern CHIP-48 behavior, true for legacy COSMAC VIP behavior
bool LEGACY_JUMP = false;
bool AMIGA_INDEX_OVERFLOW = true; // required for some games like Spacefight 2091
bool LEGACY_INDEX_SAVE = false;

///////////////////////////

#define GET_X(opcode) ((opcode & 0x0F00) >> 8)
#define GET_Y(opcode) ((opcode & 0x00F0) >> 4)
#define GET_N(opcode) (opcode & 0x000F)
#define GET_NN(opcode) (opcode & 0x00FF)
#define GET_NNN(opcode) (opcode & 0x0FFF)

// ram 4kb
uint8_t memory[4096];

// stack & stack pointer
uint16_t stack[16]; 
uint16_t sp = 0;

// V registers (16 8bit registers)
uint8_t v_register[16];

uint16_t program_counter = 0x200; // 512 is where we load the game ROM
uint16_t index_register = 0;

// timer registers
uint8_t delay_timer = 0;
uint8_t sound_timer = 0;

// 64 pixels wide and 32 pixels tall
uint8_t display[2048]; 

// keypad
uint8_t keypad[16];

// fontset
const uint8_t fontset[80] = {
0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
0x20, 0x60, 0x20, 0x20, 0x70, // 1
0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
0x90, 0x90, 0xF0, 0x10, 0x10, // 4
0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
0xF0, 0x10, 0x20, 0x40, 0x40, // 7
0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
0xF0, 0x90, 0xF0, 0x90, 0x90, // A
0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
0xF0, 0x80, 0x80, 0x80, 0xF0, // C
0xE0, 0x90, 0x90, 0x90, 0xE0, // D
0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

void load_rom (const char* filename);

int main (void) {
    for (int i = 0; i < 80; i++) {
        memory[0x050 +i] = fontset[i]; // starting from 0x050 (80 dec) because of historical convention
    }

    // load the ROM
    char rom_path[256];
    snprintf(rom_path, sizeof(rom_path), "ROMs/%s", ROM_NAME);
    load_rom(rom_path);

    // seed rand
    srand(time(NULL));

    const int scale = 20; // scaling, configurable 
    const int screen_width = 64 * scale;
    const int screen_height = 32 * scale;
    InitWindow(screen_width, screen_height, "CHIP-8");
    SetTargetFPS(60); // dont change this

    while (!WindowShouldClose()) { // detect ESC key or close button
        if (IsKeyDown(KEY_ONE))   keypad[0x1] = 1; else keypad[0x1] = 0;
        if (IsKeyDown(KEY_TWO))   keypad[0x2] = 1; else keypad[0x2] = 0;
        if (IsKeyDown(KEY_THREE)) keypad[0x3] = 1; else keypad[0x3] = 0;
        if (IsKeyDown(KEY_FOUR))  keypad[0xC] = 1; else keypad[0xC] = 0; // C

        if (IsKeyDown(KEY_Q))     keypad[0x4] = 1; else keypad[0x4] = 0;
        if (IsKeyDown(KEY_W))     keypad[0x5] = 1; else keypad[0x5] = 0;
        if (IsKeyDown(KEY_E))     keypad[0x6] = 1; else keypad[0x6] = 0;
        if (IsKeyDown(KEY_R))     keypad[0xD] = 1; else keypad[0xD] = 0; // D

        if (IsKeyDown(KEY_A))     keypad[0x7] = 1; else keypad[0x7] = 0;
        if (IsKeyDown(KEY_S))     keypad[0x8] = 1; else keypad[0x8] = 0;
        if (IsKeyDown(KEY_D))     keypad[0x9] = 1; else keypad[0x9] = 0;
        if (IsKeyDown(KEY_F))     keypad[0xE] = 1; else keypad[0xE] = 0; // E

        if (IsKeyDown(KEY_Z))     keypad[0xA] = 1; else keypad[0xA] = 0; // A
        if (IsKeyDown(KEY_X))     keypad[0x0] = 1; else keypad[0x0] = 0; // 0
        if (IsKeyDown(KEY_C))     keypad[0xB] = 1; else keypad[0xB] = 0; // B
        if (IsKeyDown(KEY_V))     keypad[0xF] = 1; else keypad[0xF] = 0; // F

        if (delay_timer > 0) delay_timer--;
        if (sound_timer > 0) {
            sound_timer--;
            printf("\a");
            fflush(stdout);
        }

        BeginDrawing(); // setup framebuffer canvas
        ClearBackground(BLACK);

        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 64; x++) {
                int index = (y * 64) + x;
                
                if (display[index] == 1) {
                    DrawRectangle(x*scale, y*scale, scale, scale, WHITE);
                }
            }
        }

        // run 10 times per FPS (60 in this case) so this executes 600 instructions per second
        for (int i = 0; i < 10; i++) {
            // fetch
            uint8_t byte1 = memory[program_counter];
            uint8_t byte2 = memory[program_counter + 1];
            uint16_t opcode = (byte1 << 8) | byte2;
            program_counter += 2;

            // decode & execute
            uint16_t category = opcode & 0xF000; // Masking off the first number (binary AND)
            switch (category) {
                case 0x0000:
                    // Clear Screen
                    if (GET_NNN(opcode) == 0x00E0) {
                        for (int p = 0; p < 2048; p++) {
                            display[p] = 0;
                        }
                    }
                    // Return 
                    else if (GET_NNN(opcode) == 0x00EE) {
                        program_counter = stack[--sp];
                    }
                break;
            
                case 0x1000:
                    // Jump
                    program_counter = GET_NNN(opcode);
                break;

                case 0x2000:
                    // Call Subroutine
                    stack[sp++] = program_counter;
                    program_counter = GET_NNN(opcode);
                break;

                case 0x3000:
                    // Skip 
                    if (v_register[GET_X(opcode)] == GET_NN(opcode)) {
                        program_counter += 2;
                    }
                break;

                case 0x4000:
                    // Skip
                    if (v_register[GET_X(opcode)] != GET_NN(opcode)) {
                        program_counter += 2;
                    }
                break;

                case 0x5000:
                    // Skip
                    if (v_register[GET_X(opcode)] == v_register[GET_Y(opcode)]) {
                        program_counter += 2;
                    }
                break;

                case 0x6000:
                    // Set
                    v_register[GET_X(opcode)] = GET_NN(opcode);
                break;

                case 0x7000:
                    // Add
                    v_register[GET_X(opcode)] += GET_NN(opcode);
                break;

                case 0x8000:
                    // Logical & Arithmetic Instructions
                    switch (opcode & 0x000F) {
                        case 0x0000:
                            // Set
                            v_register[GET_X(opcode)] = v_register[GET_Y(opcode)];
                        break;

                        case 0x0001:
                            // Binary OR
                            v_register[GET_X(opcode)] = v_register[GET_X(opcode)] | v_register[GET_Y(opcode)];
                        break;

                        case 0x0002:
                            // Binary AND
                            v_register[GET_X(opcode)] = v_register[GET_X(opcode)] & v_register[GET_Y(opcode)];
                        break;

                        case 0x0003:
                            // Logical XOR
                            v_register[GET_X(opcode)] = v_register[GET_X(opcode)] ^ v_register[GET_Y(opcode)];
                        break;

                        case 0x0004: {
                            // Add (with Carry)

                            // V[X] and V[Y] are uint8_t, we do this to catch the possible overflow
                            uint16_t sum = v_register[GET_X(opcode)] + v_register[GET_Y(opcode)];

                            // store the wrapped result back in V[X] (uint8_t) (ex, 256 becomes 0)
                            v_register[GET_X(opcode)] = sum & 0xFF;

                            // Set V[F] to 1 if it overflowed or 0 if it didnt
                            v_register[0xF] = (sum > 255) ? 1 : 0;
                            break;
                        }

                        case 0x0005: {
                            // Subtract
                            uint8_t X = GET_X(opcode);
                            uint8_t Y = GET_Y(opcode);

                            // saving original values first
                            uint8_t x_val = v_register[X];
                            uint8_t y_val = v_register[Y];

                            // C will wrap negative numbers automatically
                            v_register[X] = x_val - y_val;

                            // if we didnt underflow set V[F] to 1
                            v_register[0xF] = (x_val >= y_val) ? 1 : 0;
                            break;
                        }

                        case 0x0006: {
                            // Shift Right (Ambiguous Instruction)
                            uint8_t X = GET_X(opcode);
                            uint8_t Y = GET_Y(opcode);

                            // legacy instruction
                            if (LEGACY_SHIFT) {
                                v_register[X] = v_register[Y];
                            }

                            uint8_t dropped_bit = v_register[X] & 0x1;

                            v_register[X] >>= 1;
                            v_register[0xF] = dropped_bit;
                            break;
                        }

                        case 0x0007: {
                            // Subtract #2
                            uint8_t X = GET_X(opcode);
                            uint8_t Y = GET_Y(opcode);

                            uint8_t x_val = v_register[X];
                            uint8_t y_val = v_register[Y];

                            v_register[X] = y_val - x_val;

                            // same as 0x0005 just backwards
                            v_register[0xF] = (y_val >= x_val) ? 1 : 0;
                            break;
                        }

                        case 0x000E: {
                            // Shift Left (Ambiguous Instruction)
                            uint8_t X = GET_X(opcode);
                            uint8_t Y = GET_Y(opcode);

                            // legacy instruction
                            if (LEGACY_SHIFT) {
                                v_register[X] = v_register[Y];
                            }

                            uint8_t dropped_bit = (v_register[X] & 0x80) >> 7;

                            v_register[X] <<= 1;
                            v_register[0xF] = dropped_bit;
                            break;
                        }
                    }
                break;

                case 0x9000:
                    // Skip
                    if (v_register[GET_X(opcode)] != v_register[GET_Y(opcode)]) {
                        program_counter += 2;
                    }
                break;

                case 0xA000:
                    // Set Index
                    index_register = GET_NNN(opcode);
                break;

                case 0xB000:
                    // Jump With Offset (Ambiguous Instruction)
                    if (LEGACY_JUMP) {
                        program_counter = GET_NNN(opcode) + v_register[0];
                    } else {
                        program_counter = GET_NNN(opcode) + v_register[GET_X(opcode)];
                    }
                break;

                case 0xC000: {
                    // Random

                    // gen a number from 0 to 255
                    uint8_t random_num = rand() % (255 + 1);

                    v_register[GET_X(opcode)] = random_num & GET_NN(opcode);
                    break;  
                }

                case 0xD000: {
                    uint8_t X = GET_X(opcode);
                    uint8_t Y = GET_Y(opcode);
                    uint8_t N = GET_N(opcode); // N here is the height of the sprite

                    uint8_t x_cord = v_register[X] & (64 - 1); // wrapping around with bitmask
                    uint8_t y_cord = v_register[Y] & (32 - 1); // same thing here wrapping around (same as % 64 but in this case bitwise AND is mathematically superior)

                    // clear the collision alarm
                    v_register[0xF] = 0;

                    // loop through sprites rows
                    for (int i = 0; i < N; i++) { 
                        uint8_t sprite_byte = memory[i + index_register]; // index register is the beginning of the sprite in the memory (its just a list of bytes)

                        // stop if u reach the bottom of the screen (index starts at 0 so its >=32 not >32)
                        if (y_cord + i >= 32) {
                            break;
                        }

                        // for each of the 8 pixels/bits in this sprite row
                        for (int j = 0; j < 8; j++) {
                            // stop if you reach the right edge of the screen (clipping, the sprite itself doesnt wrap over)
                            if (x_cord + j >= 64) {
                                break;
                            }

                            // checking if the current pixel in the sprite row is on
                            if (sprite_byte & (0x80 >> j)) {
                                int screen_index = ((y_cord + i) * 64) + (x_cord + j); // got it now
                            
                                // checking if the pixel at coordinates X,Y on the screen is also on
                                if (display[screen_index] == 1) {
                                    v_register[0xF] = 1;
                                }

                                display[screen_index] ^= 1; // toggling the screen pixel
                            }
                        }
                    }

                    break;
                }

                case 0xE000: {
                    // Skip If Key
                    uint8_t X = GET_X(opcode);
                    uint8_t key = v_register[X];

                    switch (GET_NN(opcode)) {
                        case 0x9E:
                            // Skip if key is pressed
                            if (keypad[key] == 1) {
                                program_counter += 2;
                            }
                        break;

                        case 0xA1:
                            // Skip if key isnt pressed
                            if (keypad[key] == 0) {
                                program_counter += 2;
                            }
                        break;
                    }
                    break;
                }

                case 0xF000:
                    // Timers
                    switch (GET_NN(opcode)) {
                        case 0x07:
                            // Set V[X] to delay_timer
                            v_register[GET_X(opcode)] = delay_timer;
                        break;

                        case 0x15:
                            // Set delay_timer to V[X]
                            delay_timer = v_register[GET_X(opcode)];
                        break;

                        case 0x18:
                            // Set sound_timer to V[X]
                            sound_timer = v_register[GET_X(opcode)];
                        break;

                        case 0x1E: {
                            // Add To Index
                            uint8_t X = GET_X(opcode);

                            // if the amiga config is enabled, check if the addition pushes the index past 4095
                            if (AMIGA_INDEX_OVERFLOW) {
                                v_register[0xF] = (index_register + v_register[X] > 0x0FFF) ? 1 : 0;
                            }

                            index_register += v_register[X];
                            break;
                        }

                        case 0x0A: {
                            // Get Key
                            uint8_t X = GET_X(opcode);
                            bool key_pressed = false;

                            for (int k = 0; k < 16; k++) {
                                if (keypad[k] == 1) {
                                    v_register[X] = k; // save the key that was pressed
                                    key_pressed = true;
                                    break;
                                }
                            }

                            // if no key was pressed rewind pc so we run this instruction again and make a harmless loop
                            if (!key_pressed) {
                                program_counter -= 2;
                            }
                            break;
                        }

                        case 0x29: {
                            // Font Character
                            uint8_t character = v_register[GET_X(opcode)];

                            // 0x050 is the hardcoded value in memory where sprites begin and every character is 5 bytes tall
                            index_register = 0x050 + (character * 5); // so this points to the V[X] character in memory
                            break;
                        }

                        case 0x33: {
                            // Binary Coded Decimal Conversion
                            uint8_t number = v_register[GET_X(opcode)];

                            memory[index_register] = number / 100; // 100s
                            memory[index_register + 1] = (number / 10) % 10; // 10s
                            memory[index_register + 2] = number % 10; // 1s
                            break;
                        }

                        case 0x55: {
                            // Store into memory
                            uint8_t X = GET_X(opcode);

                            // V0 - VX
                            for (int i = 0; i <= X; i++) {
                                memory[index_register + i] = v_register[i];
                            }

                            // if legacy index save is on, the hardware is supposed to modify the index register
                            if (LEGACY_INDEX_SAVE) {
                                index_register = index_register + X + 1;
                            }
                            break;
                        }

                        case 0x65: {
                            // Load Memory
                            uint8_t X = GET_X(opcode);

                            // V0 - VX
                            for (int i = 0; i <= X; i++) {
                                v_register[i] = memory[index_register + i];
                            }

                            // same legacy check as 0x55
                            if (LEGACY_INDEX_SAVE) {
                                index_register = index_register + X + 1;
                            }
                            break;
                        }
                    }
                break;

            }
        }

        EndDrawing(); // swap canvas and draw 
    }

    CloseWindow(); // close window and openGL context

}

void load_rom (const char* filename) {
    FILE* file = fopen(filename, "rb");

    if (file == NULL) {
        printf("Error, could not open file %s\n", filename);
        exit(EXIT_FAILURE);
    }

    // finding out how big the file is
    fseek(file, 0, SEEK_END);
    long rom_size = ftell(file);
    rewind(file);

    // check if it fits in RAM , 4096 - 512 = 3584
    if (rom_size > (4096 - 0x200)) {
        printf("Error, ROM file is too large to fit in RAM\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    // starting from 0x200 (512), dump the bytes into the memory
    fread(&memory[0x200], 1, rom_size, file);
    fclose(file);

    printf("Successfully loaded %ld bytes of %s into memory\n", rom_size, filename);
}