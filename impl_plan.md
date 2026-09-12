# Tinosc Kernel: VFS, Storage, and Userspace Implementation Plan

## Executive Summary

This plan builds out the critical missing layers for a functional OS:
1. **Storage layer** - Hardware drivers + abstraction layer
2. **Filesystem layer** - VFS + filesystem implementations  
3. **Userspace** - Syscalls, init system, custom language interpreter

**Estimated complexity:** Medium-High | **Timeline:** 3-4 months (sequential), 1-2 months (parallel teams)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│               Custom Language Interpreter                │
│              (Userspace Application Layer)               │
├─────────────────────────────────────────────────────────┤
│     Init System / Shell (Process Management Layer)       │
├─────────────────────────────────────────────────────────┤
│        Syscall Interface (Kernel Boundary)               │
├─────────────────────────────────────────────────────────┤
│  VFS (Virtual Filesystem Abstraction Layer)              │
├─────────────────────────────────────────────────────────┤
│  Filesystem Implementations (FAT32, ext2, etc)           │
├─────────────────────────────────────────────────────────┤
│  Block Device Abstraction Layer (I/O Queueing)           │
├─────────────────────────────────────────────────────────┤
│  Storage Drivers (AHCI, NVMe)                            │
├─────────────────────────────────────────────────────────┤
│  PCI Driver (Enhanced)                                   │
├─────────────────────────────────────────────────────────┤
│               Existing Subsystems                        │
│           (GDT, IDT, PIC, PMM, VMM, etc)                │
└─────────────────────────────────────────────────────────┘
```

---

## Phase 1: Storage Infrastructure (Weeks 1-3)

### 1.1 PCI Driver Enhancement ⚙️
**Why:** Your current PCI driver is too basic for storage device detection.

**Required changes:**
- Add **mass storage class filtering** (class 0x01)
- Implement **driver matching** by vendor/device ID
- Parse **capability pointers** for MSI interrupt support
- Add **memory-mapped I/O (MMIO) region** discovery and mapping
- Enumerate all discovered devices with proper logging

**New kernel/drivers/pci/ files:**
- `pci_classes.h` - Storage class definitions
- `pci_driver.c` - Driver registration system
- `pci_mmio.c` - MMIO mapping utilities

**Estimated effort:** 2-3 days

---

### 1.2 Block Device Abstraction Layer
**Why:** Both AHCI and NVMe need a common interface to VFS.

**Design:**
```c
// kernel/drivers/blkdev/blkdev.h
struct block_device_ops {
    int (*read_sector)(struct block_device *bdev, uint64_t lba, void *buf);
    int (*write_sector)(struct block_device *bdev, uint64_t lba, void *buf);
    uint64_t (*get_capacity)(struct block_device *bdev);
};

struct block_device {
    char name[32];          // "sda", "nvme0n1"
    uint64_t capacity_lba;
    struct block_device_ops *ops;
    void *private_data;     // Driver-specific data
};
```

**Responsibilities:**
- Maintain device registry
- Queue read/write requests
- Handle DMA buffer allocation (must be page-aligned, under 4GB for ISA DMA)
- Provide caching layer (optional, start without)

**New kernel/drivers/blkdev/ files:**
- `blkdev.h` - Interface definitions
- `blkdev.c` - Registry and core functions

**Estimated effort:** 2-3 days

---

### 1.3 AHCI Driver
**Why:** AHCI is the standard protocol for SATA SSDs/HDDs.

**AHCI Basics:**
- Hardware exposes memory-mapped port registers
- Each SATA port has command queue
- Submit commands, hardware executes asynchronously, signals via interrupt
- Per-port registers: `PxCMD`, `PxCL` (command list), `PxFB` (FIS buffer)

**Implementation:**
1. **Initialization:**
   - Enable AHCI mode (via PCI config)
   - Reset controller
   - For each port: initialize command/FIS structures
   
2. **Command Submission:**
   - Build PRD (Physical Region Descriptor) table for DMA
   - Write command descriptor to command list
   - Trigger port via `PxCI` register
   
3. **Interrupt Handling:**
   - IRQ fires when command completes
   - Check `PxIS` (interrupt status)
   - Clear status, read result, complete request

**New kernel/drivers/ahci/ files:**
- `ahci.h` - Register definitions, structs
- `ahci.c` - Driver implementation
- `ahci_cmd.c` - Command building/submission

**Dependencies:** PCI enhancement, block device layer

**Estimated effort:** 1-2 weeks

---

### 1.4 NVMe Driver (Optional but Recommended)
**Why:** Modern SSDs are NVMe. AHCI won't work for them.

**NVMe Basics:**
- Queue-based interface: submission queues (SQ) and completion queues (CQ)
- Command set differs from AHCI
- Supports much higher performance (parallel commands)
- Requires interrupt coalescing setup

**Can be deferred** to later phase, but good to plan for it.

**Dependencies:** PCI enhancement, block device layer

**Estimated effort:** 2 weeks (after AHCI)

---

## Phase 2: Virtual Filesystem (Weeks 4-5)

### 2.1 VFS Design
**Why:** Abstraction layer so multiple filesystems can coexist.

**Core concepts:**
```c
// kernel/fs/vfs.h
struct inode {
    uint64_t ino;                    // Inode number
    uint32_t mode;                   // File type + permissions
    uint64_t size;                   // File size in bytes
    uint32_t uid, gid;               // Owner
    uint64_t atime, mtime, ctime;    // Timestamps
    struct superblock *sb;           // Owning filesystem
    struct inode_ops *ops;           // open, read, write, mkdir, etc
    void *private_data;              // Filesystem-specific data
};

struct file {
    struct inode *inode;
    uint64_t offset;                 // Current read/write position
    int flags;                       // O_RDONLY, O_WRONLY, O_APPEND, etc
    struct file_ops *ops;
};

struct superblock {
    struct block_device *bdev;
    struct filesystem_type *fs_type; // FAT32, ext2, etc
    void *private_data;              // Filesystem-specific data
    struct inode *root;              // Root inode
};

struct filesystem_type {
    char name[32];                   // "fat32", "ext2"
    int (*mount)(struct superblock *sb, ...);
    struct inode_ops inode_ops;
};
```

**Namespace design:**
- `/dev` - Block/character devices
- `/proc` - Kernel info (optional, early)
- `/sys` - System info (optional, later)
- `/bin, /lib, /etc` - Standard Unix hierarchy

**New kernel/fs/ files:**
- `vfs.h` - Core interfaces
- `vfs.c` - VFS implementation
- `namei.c` - Path traversal, lookup
- `file.c` - File operations
- `inode.c` - Inode management

**Estimated effort:** 3-4 days

---

### 2.2 VFS Core Functions
**Implementation:**
```c
int vfs_open(const char *path, int flags, struct file **out);
int vfs_read(struct file *f, void *buf, size_t count);
int vfs_write(struct file *f, const void *buf, size_t count);
int vfs_close(struct file *f);
int vfs_mkdir(const char *path, uint32_t mode);
int vfs_stat(const char *path, struct stat *st);
```

**Path traversal algorithm:**
1. Start at root inode
2. For each path component:
   - Call `inode->ops->lookup(parent, name)` to find child
   - Follow symlinks if needed
   - Check permissions
3. Return final inode or error

**Dependencies:** VFS design, block device layer

**Estimated effort:** 2-3 days

---

### 2.3 FAT32 Filesystem Implementation
**Why:** Simpler than ext2, good for initial testing. Widely compatible.

**FAT32 Structures:**
- Boot sector: cluster size, FAT count, root dir
- FAT (File Allocation Table): chain of clusters
- Data area: file/directory contents

**Implementation priorities:**
1. **Reading:**
   - Parse boot sector
   - Navigate directory entries
   - Follow cluster chains
   - Read file contents
   
2. **Directory traversal:**
   - Each entry is 32 bytes (name, attributes, start cluster, size)
   - Handle 8.3 names and long filenames (LFN)
   
3. **File reading:**
   - Find start cluster
   - Follow FAT chain
   - Read cluster contents via block device

**New kernel/fs/fat32/ files:**
- `fat32.h` - FAT32 structures
- `fat32.c` - Mount, inode operations
- `fat32_dir.c` - Directory traversal
- `fat32_file.c` - File reading

**Dependencies:** VFS core, block device layer

**Estimated effort:** 1 week

---

### 2.4 ext2 Filesystem Implementation (Optional)
**Why:** Industry standard, supports file permissions properly.

**Can defer to Phase 3** if you want to test FAT32 first.

**Estimated effort:** 1.5-2 weeks (more complex than FAT32)

---

### 2.5 Disk Read/Write Operations
**By this point, you have:**
- Block device layer for hardware abstraction
- VFS for filesystem navigation
- FAT32 for reading files

**Write operations require:**
- FAT table updates (cluster allocation)
- Directory entry writing
- Safe shutdown/fsync
- Journal support (for ext3/4 - optional)

**For initial phase, support read-only FAT32.** Write support can come later.

---

## Phase 3: Syscall Interface & Userspace (Weeks 6-8)

### 3.1 Kernel Syscall Interface
**Why:** Bridge between userspace and kernel.

**Design:**
```c
// kernel/syscall.h
#define SYS_exit      0
#define SYS_write     1
#define SYS_open      2
#define SYS_read      3
#define SYS_close     4
#define SYS_brk       5      // Heap growth
#define SYS_fork      6      // Process creation
#define SYS_execve    7      // Program loading
#define SYS_stat      8
// ... more as needed
```

**ABI (x86-64):**
- Syscall number in `rax`
- Args: `rdi, rsi, rdx, rcx, r8, r9`
- Return: `rax`
- Use `syscall` instruction

**Implementation:**
```c
// kernel/syscall.c
void syscall_handler(struct regs *r) {
    switch (r->rax) {
        case SYS_exit:    sys_exit(r->rdi); break;
        case SYS_write:   r->rax = sys_write(r->rdi, (void*)r->rsi, r->rdx); break;
        case SYS_open:    r->rax = sys_open((char*)r->rdi, r->rsi); break;
        // ...
    }
}
```

**New kernel/syscall/ files:**
- `syscall.h` - Syscall numbers
- `syscall.c` - Dispatcher
- `sys_io.c` - write, read syscalls
- `sys_fs.c` - open, close, stat syscalls
- `sys_process.c` - exit, fork, execve syscalls

**Estimated effort:** 3-4 days

---

### 3.2 Userspace C Library
**Why:** Standard library linking and basic syscall wrappers.

**Responsibilities:**
- Syscall wrappers (`write()` -> `syscall(SYS_write, ...)`
- Memory allocation (`malloc`, `free`)
- String functions (`strlen`, `strcmp`, `strcpy`)
- Standard I/O (`printf`, `fopen`, `fread`)
- Process functions (`exit`)
- Entry point setup (`.crt0`, `main()`)

**New libc/ files (separate from kernel):**
- `crt0.S` - Entry point, calls main
- `syscall.S` - Syscall stubs (inline asm)
- `malloc.c` - Heap allocator
- `stdio.c` - Printf, file I/O wrappers
- `string.c` - String functions
- `libc.h` - Headers

**Can be simplified:** Start with minimal libc, expand later.

**Estimated effort:** 3-4 days (minimal version)

---

### 3.3 Process Loading (ELF)
**Why:** Load and execute userspace programs from disk.

**ELF parsing:**
- Read ELF header
- Load program headers into memory
- Set up stack (arguments, environment)
- Set up heap (brk at end of data)
- Jump to entry point in userspace

**New kernel/process/ files:**
- `elf.h` - ELF structures
- `elf_load.c` - ELF parser and loader
- `process.c` - Process creation
- `sched.c` - Task scheduling (basic round-robin)

**Dependencies:** Syscall interface, userspace C library (libc)

**Estimated effort:** 1 week

---

### 3.4 Init System
**Why:** Spawn initial processes on boot.

**Simple approach:**
```c
// init.c - Userspace program
int main() {
    printf("Init starting...\n");
    
    // Start shell or services
    pid_t shell_pid = fork();
    if (shell_pid == 0) {
        execve("/bin/shell", argv, envp);
    }
    
    // Wait for children
    while (1) {
        wait(NULL);
    }
}
```

**Can write init in:**
- Custom language (once it's ready) 
- C (simpler, faster)
- Shell script (if you implement a shell)

**Estimated effort:** 1-2 days (once process loading works)

---

## Phase 4: Custom Language Interpreter (Weeks 9-12)

### 4.1 Language Design
**Recommendations:**
- **Syntax:** Python-like (easy to parse, readable)
- **Runtime:** Bytecode VM (good balance of speed/simplicity)
- **Type system:** Dynamic (simpler to implement)
- **Focus:** File I/O, process spawning, kernel interaction

**Example language (pseudocode):**
```
# Example program in TinoScript
def read_file(path):
    f = open(path, "r")
    data = f.read()
    f.close()
    return data

def main():
    print("Hello from TinoScript!")
    file_data = read_file("/etc/config")
    print(file_data)
    
main()
```

**Estimated effort:** 1-2 days (language spec only)

---

### 4.2 Lexer (Tokenizer)
**Input:** Source code text  
**Output:** Token stream

**Examples:**
- `def` → TOKEN_DEF
- `main` → TOKEN_IDENT("main")
- `123` → TOKEN_NUMBER(123)
- `"hello"` → TOKEN_STRING("hello")

**Implementation:**
```c
struct token {
    int type;           // TOKEN_DEF, TOKEN_IDENT, etc
    char *value;        // For idents, strings, numbers
};

struct token *lexer(const char *source);
```

**New userspace/lang/ files:**
- `lexer.h`, `lexer.c`

**Estimated effort:** 2-3 days

---

### 4.3 Parser
**Input:** Token stream  
**Output:** AST (Abstract Syntax Tree)

**AST node types:**
```c
struct ast_node {
    int type;  // AST_FUNCTION, AST_CALL, AST_BINOP, etc
    union {
        struct { struct ast_node *params; struct ast_node *body; } func;
        struct { struct ast_node *func; struct ast_node *args; } call;
        // ... more variants
    } data;
};
```

**Parsing algorithm:** Recursive descent parser for simple top-down parsing.

**New userspace/lang/ files:**
- `parser.h`, `parser.c`

**Estimated effort:** 3-4 days

---

### 4.4 Bytecode VM and Interpreter
**Input:** AST  
**Output:** Execution results

**Two approaches:**
1. **Direct AST interpreter** (simpler, slower)
2. **Bytecode VM** (compile AST → bytecode, then execute)

**Recommendation:** Bytecode VM for better performance and cleaner design.

**VM opcodes:**
```c
enum opcodes {
    OP_CONST,      // Push constant
    OP_LOAD,       // Load variable
    OP_STORE,      // Store variable
    OP_ADD,        // Arithmetic
    OP_CALL,       // Function call
    OP_RET,        // Return
    // ... more
};
```

**New userspace/lang/ files:**
- `vm.h`, `vm.c` - VM execution engine
- `codegen.c` - AST → bytecode compiler

**Estimated effort:** 1-2 weeks

---

### 4.5 Standard Library
**Provide functions for:**

**I/O:**
```
print(msg)
input() -> string
open(path, mode) -> file
file.read() -> string
file.write(data) -> int
file.close()
```

**Process control:**
```
spawn(program, args) -> pid
wait(pid) -> exit_code
exit(code)
```

**Utilities:**
```
len(string/list) -> int
substr(str, start, len) -> string
split(str, delim) -> list
join(list, delim) -> string
```

**Implementation:**
- Most wrap syscalls via userspace libc
- Some are pure language constructs (print → syscall write)

**New userspace/lang/ files:**
- `stdlib.c` - Standard library
- `stdlib.h` - Headers

**Estimated effort:** 3-4 days

---

### 4.6 Kernel Interaction Layer
**Goal:** Allow language programs to call kernel-specific functions.

**Example:**
```
# Spawn a background process and wait for it
def run_background(cmd):
    pid = kernel.spawn_process(cmd)
    kernel.wait(pid)
    print("Process done")
```

**Implementation:**
- Add syscall wrappers to language stdlib
- Create bindings: `kernel.spawn_process()` → `sys_fork()` + `sys_execve()`
- Validate inputs (check pointers, prevent buffer overflows)

**New userspace/lang/ files:**
- `kernel_bindings.c` - Kernel interaction layer

**Estimated effort:** 2-3 days

---

## Phase 5: Integration & Testing (Weeks 13-16)

### 5.1 Full Boot & Integration Test

**Boot sequence:**
1. Kernel starts, initializes subsystems
2. VFS mounts root filesystem from disk
3. Kernel spawns `/bin/init`
4. Init runs userspace programs
5. Custom language interpreter runs scripts

**Test scenario:**
```bash
$ /bin/shell  # Shell or custom language REPL
> var data = read_file("/etc/config")  # Read from disk
> print(data)                           # Print to stdout
> spawn("/bin/game")                    # Start another program
```

**What to verify:**
- Keyboard input works (already fixed!)
- Files read correctly from disk
- Language programs execute
- Multiple processes can run
- Disk persistence (data survives reboot)

---

## Implementation Order (Recommended)

**Hard dependencies drive this order:**

1. **pci-enhance** ← Start here
2. **diskio-layer** ← Blocks all storage work
3. **ahci-driver** ← Enables disk access
4. **vfs-design** + **vfs-core** ← Foundation for filesystem
5. **fat32-fs** ← Read-only FAT32
6. **syscall-iface** ← Enables userspace
7. **init-loader** ← Load userspace programs
8. **userspace-lib** ← Basic libc
9. **lang-lexer, lang-parser, lang-vm** ← Parallel
10. **lang-stdlib** ← Language utilities
11. **lang-kernel-bindings** ← Kernel interaction
12. **init-script** ← Final piece
13. **integration-test** ← Full system test

---

## Effort Estimates

| Component | Effort | Notes |
| --- | --- | --- |
| PCI Enhancement | 2-3d | Straightforward |
| Block Device Layer | 2-3d | Straightforward |
| AHCI Driver | 1-2w | Largest datasheet to learn |
| NVMe Driver | 2w | Can defer |
| VFS Design + Core | 1w | Careful design pays off |
| FAT32 FS | 1w | Good starter filesystem |
| ext2 FS | 1.5-2w | Can defer |
| Syscall Interface | 3-4d | Straightforward |
| Userspace libc | 3-4d | Simplified version |
| Process Loading | 1w | ELF parser + scheduling |
| Init System | 1-2d | Simple until multitasking is solid |
| **Language (total)** | **4-6w** | Lexer, parser, VM are sequential |
| Integration Testing | 1-2w | Debugging full stack |
| **TOTAL** | **3-4 months** | Sequential |

---

## Known Challenges

1. **DMA buffer management** - Block device layer must allocate safe DMA buffers (page-aligned, <4GB)
2. **Interrupt handling** - AHCI generates many interrupt types; must handle correctly
3. **Filesystem caching** - Performance will suck without caching; implement only if needed
4. **Language performance** - Bytecode VM might be slow; optimize opcodes if needed
5. **Process scheduling** - Once multiple processes run, scheduling bugs are hard to debug

---

## Recommendations

✅ **Do this first:**
- PCI → Block device → AHCI path (gets you disk I/O)
- VFS → FAT32 (gets you filesystem)
- Syscall interface (enables userspace)

⏭️ **Defer to Phase 2:**
- NVMe driver (AHCI is enough initially)
- ext2 filesystem (FAT32 works for testing)
- File write operations (read-only initially)
- Language optimizations (correctness > speed first)

🎯 **Quick wins for testing:**
- Hard-code a test program on disk to verify ELF loading
- Use FAT32 for simplicity over performance
- Start with single-process init, add multitasking later

---

## File Structure Reference

```
kernel/
├── drivers/
│   ├── pci/              # [ENHANCE] PCI driver
│   ├── blkdev/           # [NEW] Block device layer
│   ├── ahci/             # [NEW] AHCI driver
│   └── nvme/             # [DEFER] NVMe driver
├── fs/
│   ├── vfs.h/c           # [NEW] Virtual filesystem
│   ├── namei.c           # [NEW] Path traversal
│   ├── fat32/            # [NEW] FAT32 implementation
│   └── ext2/             # [DEFER] ext2 implementation
├── syscall/              # [NEW] Syscall interface
└── process/              # [NEW] Process management

libc/                      # [NEW] Userspace C library
├── crt0.S
├── syscall.S
├── malloc.c
└── stdio.c

userspace/lang/           # [NEW] Custom language
├── lexer.c
├── parser.c
├── vm.c
└── stdlib.c
```

---