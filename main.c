#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <raylib.h>
#include <stdio.h>

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
    load_rom("ROMs/ibm_logo.ch8");

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
                        for (int i = 0; i < 2048; i++) {
                            display[i] = 0;
                        }
                    }
                break;
            
                case 0x1000:
                    // Jump
                    program_counter = GET_NNN(opcode);
                break;

                case 0x2000:
                
                break;

                case 0x3000:
                
                break;

                case 0x4000:
                
                break;

                case 0x5000:
                
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
                
                break;

                case 0x9000:
                
                break;

                case 0xA000:
                    // Set Index
                    index_register = GET_NNN(opcode);
                break;

                case 0xB000:
     
                break;

                case 0xC000:
              
                break;

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
                                int screen_index = ((y_cord + i) * 64) + (x_cord + j); // dont understand this at all , like idk what the variables even mean
                            
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
                case 0xE000:
             
                break;

                case 0xF000:
               
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