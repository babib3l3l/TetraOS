#ifndef INPUT_H
#define INPUT_H

void handle_input();
int keyboard_read_scancode();
void process_command(const char* input);

char keyboard_get_char(void);
char get_input_char();

#endif