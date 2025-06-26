#include <terminal.hpp>
#include <string.hpp>

// Global terminal instance for keyboard handler
Terminal* g_terminal = nullptr;

// Keyboard handler function that's registered with the interrupt system
void terminal_keyboard_handler() {
    if (g_terminal) {
        g_terminal->handleKeypress();
    }
}

Terminal::Terminal() {
    inputPosition = 0;
    readingInput = false;
    
    // Initialize input buffer
    for (int i = 0; i < 256; i++) {
        inputBuffer[i] = 0;
    }
    
    // Set global terminal pointer
    g_terminal = this;
}

void Terminal::init(Vga* vga) {
    this->vga = vga;
    vga->clear();
}

void Terminal::setupInterrupts(Interrupts* interrupts) {
    this->interrupts = interrupts;
    
    // Register keyboard handler for IRQ1
    interrupts->registerHandler(1, terminal_keyboard_handler);
}

void Terminal::run() {
    readingInput = true;
    showPrompt();
    
    // Wait for keyboard interrupts
    while (true) {
        asm volatile("hlt");
    }
}

void Terminal::putChar(char c) {
    vga->putChar(c);
}

void Terminal::putString(const char* str) {
    for (int i = 0; str[i]; i++) {
        putChar(str[i]);
    }
}

void Terminal::handleKeypress() {
    // Get the key that was pressed
    char key = getLastKey();
    
    if (key == 0) {
        return; // No valid key pressed
    }
    
    if (readingInput) {
        if (key == '\n') { // Enter key
            //putChar('\n');
            vga->set_xy(vga->get_x(), vga->get_y() + 1);
            // Null-terminate the input
            inputBuffer[inputPosition] = '\0';
            
            // Process the command
            processCommand(inputBuffer);
            
            // Reset input buffer
            inputPosition = 0;
            inputBuffer[0] = '\0';
            
            // Show a new prompt
            showPrompt();
        } else if (key == '\b') { // Backspace
            if(inputPosition > 0){
                inputPosition--;
                inputBuffer[inputPosition] = 0;
                // Update display (erase the character on screen)
                // Let VGA handle the cursor position
                vga->putCharAt('\0', vga->get_x() - 1, vga->get_y());
                vga->set_xy(vga->get_x() - 1, vga->get_y());
            }    
        } else if (inputPosition < 255) { // Regular character
            inputBuffer[inputPosition] = key;
            inputPosition++;
            
            // Echo the character
            putChar(key);
        }
    }
}

void Terminal::processCommand(const char* cmd) {
    if (strcmp(cmd, "clear") == 0) {
        vga->clear();
    } else if (strcmp(cmd, "help") == 0) {
        putString("\n");
        putString("Available commands:\n");
        putString("  clear - Clear the screen\n");
        putString("  help  - Show this help message\n");
        putString("  mrhakos - Show mrhakos information\n");
    } else if (strcmp(cmd, "mrhakos") == 0) {
        putString("\n");
        putString("Name: MrHakOS\n");
        putString("Author : Mohamed Hakkou\n");
        putString("Version : 0.1\n");
        putString("Description : A simple operating system using C++\n");
        putString("\n");
    }   else if (cmd[0] != '\0') {
        putChar('\n');
        putString("Unknown command: ");
        putString(cmd);
        putChar('\n');
    }
}

void Terminal::showPrompt() {
    // Show prompt
    const char* prompt = "MrHakOS >> ";
    putString(prompt);
}

void Terminal::onKeypress() {
    handleKeypress();
}
