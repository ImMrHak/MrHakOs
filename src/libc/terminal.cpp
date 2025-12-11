#include <terminal.hpp>
#include <string.hpp>
#include <interrupts.hpp>


// Global terminal instance for keyboard handler
Terminal* g_terminal = nullptr;

// Keyboard handler function that's registered with the interrupt system
void terminal_keyboard_handler() {
    if (g_terminal) {
        g_terminal->handleKeypress();
    }
}

Terminal::Terminal() {
    // Debug: show constructor entry/exit in VGA for crash isolation
    volatile unsigned short* vga = (unsigned short*)0xB8000;
    vga[30] = 0x1F55; // 'U' marks Terminal ctor start
    inputPosition = 0;
    vga[32] = 0x1F31; // '1' after inputPosition set
    readingInput = false;
    vga[33] = 0x1F32; // '2' after readingInput set
    commandReady = false;
    
    // Initialize input buffer (use volatile to avoid compiler replacing with builtin memset)
    vga[34] = 0x1F33; // '3' before buffer init loop
    volatile char* buf = inputBuffer;
    for (int i = 0; i < 256; i++) {
        buf[i] = 0;
    }
    vga[35] = 0x1F34; // '4' after buffer init loop
    
    // Set global terminal pointer
    g_terminal = this;
    vga[36] = 0x1F35; // '5' after g_terminal assignment
    vga[31] = 0x1F56; // 'V' marks Terminal ctor end
}

void Terminal::init(Vga* vga, Interrupts* interrupts, FileSystem* filesystem) {
    this->vga = vga;
    this->interrupts = interrupts;
    this->filesystem = filesystem;
    interrupts->registerHandler(1, terminal_keyboard_handler);
    vga->clear();
}

void Terminal::run() {
    readingInput = true;
    putString("Welcome MrHakOs Terminal\n");
    showPrompt();
    
    // Wait for keyboard interrupts
    while (true) {
        // Process command outside interrupt context to avoid long ISR work
        if (commandReady) {
            commandReady = false;
            inputBuffer[inputPosition] = '\0';
            processCommand(inputBuffer);
            inputPosition = 0;
            inputBuffer[0] = '\0';
            showPrompt();
        }
        asm volatile("hlt");
    }
}

void Terminal::putChar(char c) {
    vga->putChar(c);
}

void Terminal::putString(const char* str) {
    vga->set_cursor_enabled(false);
    for (int i = 0; str[i]; i++) {
        putChar(str[i]);
    }
    vga->set_cursor_enabled(true);
    vga->force_update_cursor();
}

void Terminal::handleKeypress() {
    char key = getLastKey();
    
    if (key == 0) {
        return;
    }
    
    if (readingInput) {
        if (key == '\n' || key == '\r') {
            commandReady = true;
        } else if (key == '\b') {
            if (inputPosition > 0) {
                inputPosition--;
                inputBuffer[inputPosition] = 0;
                int cx = vga->get_x();
                int cy = vga->get_y();
                if (cx > 0) {
                    cx--;
                } else if (cy > 0) {
                    cy--;
                    cx = 79; // move to last column of previous line
                } else {
                    // Top-left corner: nothing to erase
                }
                // Erase character visually and move cursor
                vga->putCharAt(' ', cx, cy);
                vga->set_xy(cx, cy);
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
    // Use static buffers instead of stack arrays to avoid 64-bit stack issues
    // Static variables are zero-initialized by default in .bss section
    static char command[256];
    static char args[256];
    
    // Clear buffers for reuse
    command[0] = '\0';
    args[0] = '\0';
    
    // Find the first space to separate command from arguments
    int i = 0;
    while (cmd[i] != ' ' && cmd[i] != '\0') {
        command[i] = cmd[i];
        i++;
    }
    command[i] = '\0';
    
    
    // Extract arguments if any
    if (cmd[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (cmd[i] != '\0') {
            args[j] = cmd[i];
            i++;
            j++;
        }
        args[j] = '\0';
    }

    // Process commands
    if (strcmp(command, "clear") == 0) {
        vga->clear();
        putString("Welcome MrHakOs Terminal\n");
    } else if (strcmp(command, "help") == 0) {
        putString("\n");
        putString("Available commands:\n");
        putString("  clear   - Clear the screen\n");
        putString("  help    - Show this help message\n");
        putString("  mrhakos - Show MrHakOs information\n");
        putString("  mkdir   - Create a new directory\n");
        putString("  ls      - List files and directories\n");
        putString("  cd      - Change current directory\n");
        putString("  touch   - Create a new .hak file\n");
        putString("  cat     - Display the content of a .hak file\n");
        putString("  echo    - Display text or write to a .hak file using > redirection\n");
        putString("  cp      - Copy a file\n");
        putString("  mv      - Move a file or directory\n");
    } else if (strcmp(command, "mrhakos") == 0) {
        putString("\n");
        putString("Name: MrHakOS\n");
        putString("Author : Mohamed Hakkou\n");
        putString("Version : 5.0\n");
        putString("Description : A simple operating system using C++\n");
    } else if (strcmp(command, "mkdir") == 0) {
        cmdMkdir(args);
    } else if (strcmp(command, "ls") == 0) {
        cmdLs(args);
    } else if (strcmp(command, "cd") == 0) {
        cmdCd(args);
    } else if (strcmp(command, "cp") == 0) {
        cmdCp(args);
    } else if (strcmp(command, "mv") == 0) {
        cmdMv(args);
    } else if (strcmp(command, "touch") == 0) {
        cmdTouch(args);
    } else if (strcmp(command, "cat") == 0) {
        cmdCat(args);
    } else if (strcmp(command, "echo") == 0) {
        cmdEcho(args);
    } else if (strcmp(command, "") == 0){
        putString("\n");
    } else if (command[0] != '\0') {
        putString("\nUnknown command: ");
        putString(command);
        putString("\n");
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

// Command handler methods
void Terminal::cmdMkdir(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: mkdir requires a directory name\n");
        return;
    }
    
    if (filesystem->mkdir(args)) {
        putString("\nDirectory created: ");
        putString(args);
        putString("\n");
    } else {
        putString("\nError: Could not create directory\n");
    }
}

void Terminal::cmdLs(const char* args) {
    // Unused parameter
    (void)args;
    putString("\n");
    
    // Get current directory
    FileSystemEntry* currentDir = filesystem->getCurrentDirectory();
    
    // Display current path
    char path[256];
    filesystem->getCurrentPath(path, 256);
    putString("Contents of ");
    putString(path);
    putString(":\n");
    
    // Check if directory is empty
    if (currentDir->childCount == 0) {
        putString("  <empty>\n");
        return;
    }
    
    // List all entries in current directory
    for (size_t i = 0; i < currentDir->childCount; i++) {
        FileSystemEntry* entry = currentDir->children[i];
        
        putString("  ");
        
        // Print entry name
        putString(entry->name);
        
        // Add trailing slash for directories
        if (entry->type == FS_TYPE_DIRECTORY) {
            putChar('/');
        }
        
        putString("\n");
    }
}

void Terminal::cmdCd(const char* args) {
    if (args[0] == '\0') {
        // No arguments, show current path
        char path[256];
        filesystem->getCurrentPath(path, 256);
        putString("\nCurrent directory: ");
        putString(path);
        putString("\n");
        return;
    }
    
    if (filesystem->cd(args)) {
        // Success, show new path
        char path[256];
        filesystem->getCurrentPath(path, 256);
        putString("\nChanged to: ");
        putString(path);
        putString("\n");
    } else {
        putString("\nError: Could not change directory\n");
    }
}

void Terminal::cmdCp(const char* args) {
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char source[256];
    static char destination[256];
    
    // Clear buffers
    source[0] = '\0';
    destination[0] = '\0';
    
    // Find the first space to separate source from destination
    int i = 0;
    while (args[i] != ' ' && args[i] != '\0') {
        source[i] = args[i];
        i++;
    }
    source[i] = '\0';
    
    // Extract destination if any
    if (args[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (args[i] != '\0') {
            destination[j] = args[i];
            i++;
            j++;
        }
        destination[j] = '\0';
    }
    
    if (source[0] == '\0' || destination[0] == '\0') {
        putString("\nError: cp requires source and destination\n");
        return;
    }
    
    if (filesystem->cp(source, destination)) {
        putString("\nFile copied: ");
        putString(source);
        putString(" -> ");
        putString(destination);
        putString("\n");
    } else {
        putString("\nError: Could not copy file. Make sure source exists and destination doesn't, and both are .hak files.\n");
    }
}

void Terminal::cmdMv(const char* args) {
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char source[256];
    static char destination[256];
    
    // Clear buffers
    source[0] = '\0';
    destination[0] = '\0';
    
    // Find the first space to separate source from destination
    int i = 0;
    while (args[i] != ' ' && args[i] != '\0') {
        source[i] = args[i];
        i++;
    }
    source[i] = '\0';
    
    // Extract destination if any
    if (args[i] == ' ') {
        int j = 0;
        i++; // Skip the space
        while (args[i] != '\0') {
            destination[j] = args[i];
            i++;
            j++;
        }
        destination[j] = '\0';
    }
    
    if (source[0] == '\0' || destination[0] == '\0') {
        putString("\nError: mv requires source and destination\n");
        return;
    }
    
    if (filesystem->mv(source, destination)) {
        putString("\nMoved/renamed: ");
        putString(source);
        putString(" -> ");
        putString(destination);
        putString("\n");
    } else {
        putString("\nError: Could not move/rename. Make sure source exists, destination doesn't, and .hak files keep their extension.\n");
    }
}

void Terminal::cmdTouch(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: touch requires a filename\n");
        return;
    }
    
    // Check if the filename has .hak extension
    const char* extension = ".hak";
    size_t argsLen = strlen(args);
    size_t extLen = strlen(extension);
    
    if (argsLen <= extLen || strcmp(args + argsLen - extLen, extension) != 0) {
        putString("\nError: Only .hak files are supported\n");
        return;
    }
    
    // Create an empty .hak file
    if (filesystem->createHakFile(args, "")) {
        putString("\nFile created: ");
        putString(args);
        putString("\n");
    } else {
        putString("\nError: Could not create file\n");
    }
}

void Terminal::cmdCat(const char* args) {
    if (args[0] == '\0') {
        putString("\nError: cat requires a filename\n");
        return;
    }
    
    // Buffer to store file content
    char buffer[FS_MAX_FILE_SIZE];
    
    // Read file content
    if (filesystem->readHakFile(args, buffer, FS_MAX_FILE_SIZE)) {
        putString("\nContent of ");
        putString(args);
        putString(":\n");
        
        // Display file content
        if (buffer[0] == '\0') {
            putString("  <empty file>\n");
        } else {
            putString(buffer);
            putString("\n");
        }
    } else {
        putString("\nError: Could not read file\n");
    }
}

void Terminal::cmdEcho(const char* args) {
    if (args[0] == '\0') {
        putString("\n");
        return;
    }
    
    // Use static buffers instead of stack arrays to avoid 64-bit stack overflow
    static char text[256];
    static char filename[256];
    
    // Clear buffers
    text[0] = '\0';
    filename[0] = '\0';
    bool redirect = false;
    
    // Find the '>' character for redirection
    int i = 0;
    int textEnd = 0;
    while (args[i] != '\0') {
        if (args[i] == '>') {
            redirect = true;
            textEnd = i;
            break;
        }
        i++;
    }
    
    if (redirect) {
        // Extract the text to echo (excluding trailing spaces)
        for (i = 0; i < textEnd; i++) {
            text[i] = args[i];
        }
        text[textEnd] = '\0';
        
        // Trim trailing spaces
        while (textEnd > 0 && text[textEnd - 1] == ' ') {
            text[--textEnd] = '\0';
        }
        
        // Extract the filename (skipping '>' and leading spaces)
        i = textEnd + 1;
        while (args[i] == ' ' || args[i] == '>') {
            i++;
        }
        
        int j = 0;
        while (args[i] != '\0') {
            filename[j++] = args[i++];
        }
        filename[j] = '\0';
        
        // Check if filename is empty
        if (filename[0] == '\0') {
            putString("\nError: No filename specified for redirection\n");
            return;
        }
        
        // Check if the filename has .hak extension
        if (!filesystem->hasExtension(filename, ".hak")) {
            putString("\nError: Only .hak files are supported\n");
            return;
        }
        
        // Write to file
        if (filesystem->createHakFile(filename, text)) {
            putString("\nContent written to ");
            putString(filename);
            putString("\n");
        } else {
            putString("\nError: Could not write to file\n");
        }
    } else {
        // Just echo the text
        putString("\n");
        putString(args);
        putString("\n");
    }
}
