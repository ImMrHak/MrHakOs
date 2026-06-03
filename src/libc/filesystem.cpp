#include <filesystem.hpp>
#include <string.hpp>

static inline void hidden_vga_touch(size_t cell) {
    volatile unsigned short* vga = (unsigned short*)0xB8000;
    vga[cell] = 0x0F20;
}

// Custom implementation of operator new for our freestanding environment.
void* operator new(size_t size) {
    static char memory_pool[FS_MEMORY_POOL_SIZE];
    static size_t next_free = 0;
    hidden_vga_touch(41);

    // Align allocation to 16 bytes for 64-bit safety.
    next_free = (next_free + 15) & ~((size_t)15);

    if (next_free + size > sizeof(memory_pool)) {
        return nullptr;
    }

    void* ptr = &memory_pool[next_free];
    next_free += size;
    next_free = (next_free + 15) & ~((size_t)15);

    return ptr;
}

void* operator new[](size_t size) {
    return operator new(size);
}

// This simple bump allocator never frees individual allocations yet.
void operator delete(void* ptr) noexcept {
    (void)ptr;
}

void operator delete(void* ptr, size_t size) noexcept {
    (void)ptr;
    (void)size;
}

void operator delete[](void* ptr) noexcept {
    (void)ptr;
}

void operator delete[](void* ptr, size_t size) noexcept {
    (void)ptr;
    (void)size;
}

// Global filesystem instance
FileSystem* g_filesystem = nullptr;

FileSystem::FileSystem() {
    hidden_vga_touch(20);
    root = nullptr;
    hidden_vga_touch(22);
    currentDirectory = nullptr;
    hidden_vga_touch(23);
    hidden_vga_touch(24);
    hidden_vga_touch(21);
    hidden_vga_touch(25);
}

void FileSystem::init() {
    hidden_vga_touch(42);
    root = new FileSystemEntry;
    hidden_vga_touch(43);
    initEntry(root, "/", FS_TYPE_DIRECTORY, nullptr);
    hidden_vga_touch(44);
    currentDirectory = root;
    hidden_vga_touch(45);
    g_filesystem = this;
    hidden_vga_touch(46);
    hidden_vga_touch(47);
}

void FileSystem::initEntry(FileSystemEntry* entry, const char* name, FileType type, FileSystemEntry* parent) {
    hidden_vga_touch(48);
    // Copy name (with bounds checking)
    size_t nameLen = strlen(name);
    if (nameLen >= FS_MAX_NAME_LENGTH) {
        nameLen = FS_MAX_NAME_LENGTH - 1;
    }
    
    for (size_t i = 0; i < nameLen; i++) {
        entry->name[i] = name[i];
    }
    entry->name[nameLen] = '\0';
    
    // Set type and parent
    entry->type = type;
    entry->parent = parent;
    
    // Initialize directory metadata only; avoid blanket zeroing children array
    // We rely on childCount to control iteration and set children entries lazily.
    if (type == FS_TYPE_DIRECTORY) {
        entry->childCount = 0;
    }
    hidden_vga_touch(49);
}

bool FileSystem::mkdir(const char* name) {
    // Check if name is valid
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    
    // Check if directory already exists
    if (findEntry(name, currentDirectory) != nullptr) {
        return false; // Entry with this name already exists
    }
    
    // Check if current directory has space for a new entry
    if (currentDirectory->childCount >= FS_MAX_FILES) {
        return false; // Directory is full
    }
    
    // Allocate memory for new directory
    // Avoid implicit zero-initialization that may use problematic memset
    FileSystemEntry* newDir = new FileSystemEntry;
    
    // Check if memory allocation failed
    if (newDir == nullptr) {
        return false; // Out of memory
    }
    
    // Initialize new directory
    initEntry(newDir, name, FS_TYPE_DIRECTORY, currentDirectory);
    
    // Add new directory to current directory's children
    currentDirectory->children[currentDirectory->childCount] = newDir;
    currentDirectory->childCount++;
    
    return true;
}

bool FileSystem::cd(const char* path) {
    // Handle special cases
    if (strcmp(path, "/") == 0) {
        // Change to root directory
        currentDirectory = root;
        return true;
    }
    
    if (strcmp(path, ".") == 0) {
        // Stay in current directory
        return true;
    }
    
    if (strcmp(path, "..") == 0) {
        // Go up one level
        if (currentDirectory->parent != nullptr) {
            currentDirectory = currentDirectory->parent;
            return true;
        }
        return false; // Already at root
    }
    
    // Find directory in current directory
    FileSystemEntry* entry = findEntry(path, currentDirectory);
    
    if (entry != nullptr && entry->type == FS_TYPE_DIRECTORY) {
        currentDirectory = entry;
        return true;
    }
    
    return false; // Directory not found or not a directory
}

void FileSystem::ls() {
    // List all entries in current directory
    for (size_t i = 0; i < currentDirectory->childCount; i++) {
        FileSystemEntry* entry = currentDirectory->children[i];
        
        // Print entry type and name
        if (entry->type == FS_TYPE_DIRECTORY) {
            // Directory entries are handled by the terminal's cmdLs method
            // which will add the trailing slash when displaying
        } else if (entry->type == FS_TYPE_HAK_FILE) {
            // .hak files are handled by the terminal's cmdLs method
            // which will display the file name with its extension
        }
    }
}

FileSystemEntry* FileSystem::findEntry(const char* name, FileSystemEntry* directory) {
    // Search for entry with given name in the specified directory
    for (size_t i = 0; i < directory->childCount; i++) {
        if (strcmp(directory->children[i]->name, name) == 0) {
            return directory->children[i];
        }
    }
    
    return nullptr; // Entry not found
}

void FileSystem::getCurrentPath(char* buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    
    // Start with empty string
    buffer[0] = '\0';
    
    // If we're at root, just return "/"
    if (currentDirectory == root) {
        if (bufferSize > 1) {
            buffer[0] = '/';
            buffer[1] = '\0';
        }
        return;
    }
    
    // Build path by traversing up to root
    FileSystemEntry* path[FS_MAX_PATH_DEPTH];
    int depth = 0;
    
    FileSystemEntry* current = currentDirectory;
    while (current != root && depth < FS_MAX_PATH_DEPTH) {
        path[depth++] = current;
        current = current->parent;
    }
    
    // Start with root
    size_t pos = 0;
    buffer[pos++] = '/';
    
    // Add path components from root to current directory
    for (int i = depth - 1; i >= 0 && pos < bufferSize - 1; i--) {
        // Add directory name
        const char* name = path[i]->name;
        size_t nameLen = strlen(name);
        
        for (size_t j = 0; j < nameLen && pos < bufferSize - 1; j++) {
            buffer[pos++] = name[j];
        }
        
        // Add separator if not the last component
        if (i > 0 && pos < bufferSize - 1) {
            buffer[pos++] = '/';
        }
    }
    
    // Null-terminate
    buffer[pos] = '\0';
}

bool FileSystem::cp(const char* source, const char* destination) {
    // Find source file in current directory
    FileSystemEntry* sourceEntry = findEntry(source, currentDirectory);
    
    // Check if source exists and is a .hak file
    if (sourceEntry == nullptr || sourceEntry->type != FS_TYPE_HAK_FILE) {
        return false; // Source not found or not a file
    }
    
    // Parse destination to see if it contains a directory path
    FileSystemEntry* targetDir = currentDirectory;
    const char* targetFilename = destination;
    
    // Check if destination contains a directory separator (/)
    const char* lastSlash = nullptr;
    for (int i = 0; destination[i] != '\0'; i++) {
        if (destination[i] == '/') {
            lastSlash = &destination[i];
        }
    }
    
    // If there's a slash, we need to navigate to the target directory
    if (lastSlash != nullptr) {
        // Extract directory name
        static char dirName[FS_MAX_NAME_LENGTH];
        int dirLen = lastSlash - destination;
        if (dirLen >= FS_MAX_NAME_LENGTH) {
            return false; // Directory name too long
        }
        
        for (int i = 0; i < dirLen; i++) {
            dirName[i] = destination[i];
        }
        dirName[dirLen] = '\0';
        
        // Find the target directory
        targetDir = findEntry(dirName, currentDirectory);
        if (targetDir == nullptr || targetDir->type != FS_TYPE_DIRECTORY) {
            return false; // Target directory not found
        }
        
        // Filename is after the slash
        targetFilename = lastSlash + 1;
    }
    
    // Check if destination already exists in target directory
    if (findEntry(targetFilename, targetDir) != nullptr) {
        return false; // Destination already exists
    }
    
    // Check if destination has .hak extension
    if (!hasExtension(targetFilename, ".hak")) {
        return false; // Only .hak files supported
    }
    
    // Save current directory and switch to target
    FileSystemEntry* savedDir = currentDirectory;
    currentDirectory = targetDir;
    
    // Create new file with copied content
    bool result = createHakFile(targetFilename, sourceEntry->file.content);
    
    // Restore current directory
    currentDirectory = savedDir;
    
    return result;
}

bool FileSystem::mv(const char* source, const char* destination) {
    // Find source entry in current directory
    FileSystemEntry* sourceEntry = findEntry(source, currentDirectory);
    
    // Check if source exists
    if (sourceEntry == nullptr) {
        return false; // Source not found
    }
    
    // Check if destination already exists
    if (findEntry(destination, currentDirectory) != nullptr) {
        return false; // Destination already exists
    }
    
    // For files, check .hak extension
    if (sourceEntry->type == FS_TYPE_HAK_FILE) {
        if (!hasExtension(destination, ".hak")) {
            return false; // Only .hak files supported
        }
    }
    
    // Rename the entry (simple name change)
    size_t nameLen = strlen(destination);
    if (nameLen >= FS_MAX_NAME_LENGTH) {
        return false; // Name too long
    }
    
    // Copy new name
    for (size_t i = 0; i < nameLen; i++) {
        sourceEntry->name[i] = destination[i];
    }
    sourceEntry->name[nameLen] = '\0';
    
    return true;
}

FileSystemEntry* FileSystem::getCurrentDirectory() {
    return currentDirectory;
}

bool FileSystem::hasExtension(const char* filename, const char* extension) {
    if (filename == nullptr || extension == nullptr) {
        return false;
    }
    
    size_t filenameLen = strlen(filename);
    size_t extensionLen = strlen(extension);
    
    // Check if filename is long enough to have the extension
    if (filenameLen <= extensionLen) {
        return false;
    }
    
    // Compare the end of the filename with the extension
    return strcmp(filename + filenameLen - extensionLen, extension) == 0;
}

bool FileSystem::createHakFile(const char* name, const char* content) {
    // Check if name is valid
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    
    // Check if file has .hak extension
    if (!hasExtension(name, ".hak")) {
        return false; // Not a .hak file
    }
    
    FileSystemEntry* file = findEntry(name, currentDirectory);
    if (file != nullptr && file->type != FS_TYPE_HAK_FILE) {
        return false; // A directory or unsupported entry already uses this name.
    }

    if (file == nullptr) {
        // Check if current directory has space for a new entry.
        if (currentDirectory->childCount >= FS_MAX_FILES) {
            return false; // Directory is full.
        }

        // Allocate memory for new file.
        // Avoid implicit zero-initialization that may use problematic memset.
        file = new FileSystemEntry;
        if (file == nullptr) {
            return false; // Out of memory.
        }

        initEntry(file, name, FS_TYPE_HAK_FILE, currentDirectory);
        currentDirectory->children[currentDirectory->childCount] = file;
        currentDirectory->childCount++;
    }

    // Set or replace file content. The bump allocator does not reclaim the old
    // content buffer yet, but overwriting keeps shell semantics predictable.
    size_t contentLen = strlen(content);
    if (contentLen > FS_MAX_FILE_SIZE - 1) {
        contentLen = FS_MAX_FILE_SIZE - 1; // Truncate if too long.
    }

    char* newContent = new char[contentLen + 1];
    if (newContent == nullptr) {
        return false;
    }

    for (size_t i = 0; i < contentLen; i++) {
        newContent[i] = content[i];
    }
    newContent[contentLen] = '\0';
    file->file.content = newContent;
    file->file.contentSize = contentLen;

    return true;
}

bool FileSystem::readHakFile(const char* name, char* buffer, size_t bufferSize) {
    // Check parameters
    if (name == nullptr || buffer == nullptr || bufferSize == 0) {
        return false;
    }
    
    // Find file in current directory
    FileSystemEntry* entry = findEntry(name, currentDirectory);
    
    // Check if file exists and is a .hak file
    if (entry == nullptr || entry->type != FS_TYPE_HAK_FILE) {
        return false; // File not found or not a .hak file
    }
    
    // Copy content to buffer
    size_t copySize = entry->file.contentSize;
    if (copySize >= bufferSize) {
        copySize = bufferSize - 1; // Leave space for null terminator
    }
    
    for (size_t i = 0; i < copySize; i++) {
        buffer[i] = entry->file.content[i];
    }
    buffer[copySize] = '\0';
    
    return true;
}