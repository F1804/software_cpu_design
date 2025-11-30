# Tiny16 Software CPU  
**CMPE 220 – System Software (FA25)**  
A complete software-implemented 16-bit CPU with assembler, emulator, memory-mapped I/O, timer hardware, and example programs.

---

## 🚀 Project Overview

This project implements a simple **16-bit CPU (Tiny16)** in C++, including:

- Instruction Set Architecture (ISA)
- CPU core (registers, ALU, flags, control unit)
- Memory + MMIO (UART + Timer)
- A two-pass assembler
- An emulator capable of loading, execauting, and dumping memory
- Example assembly programs:
  - `hello.asm`
  - `timer.asm`
  - `fib.asm`

The project demonstrates fetch–decode–execute cycles, branching, arithmetic/logic, memory addressing, stack calls, and interaction with hardware-like components.

---

## 📂 Project Structure

```
software_cpu_design/
│
├── tiny16.cpp          # Main CPU, assembler, emulator source code
├── examples/
│   ├── hello.asm       # UART Hello World
│   ├── timer.asm       # MMIO Timer example
│   └── fib.asm         # Fibonacci generator
└── README.md           # Project instructions
```

---

## 🔧 Requirements

- macOS or Linux  
- g++ with C++17 support  
- Terminal / shell environment  

---

## 🛠 Compilation

Compile the CPU and assembler/emulator using:

```bash
g++ -std=c++17 -O2 -o tiny16 tiny16.cpp
```

This will generate the executable:

```
tiny16
```

---

## ▶️ Running Example Programs

### 1. **Hello World Example**

Runs a simple UART output routine.

```bash
./tiny16 run examples/hello.asm
```

**Expected Output:**

```
Hello, World!
```

---

### 2. **Timer Example**

Demonstrates memory-mapped I/O and timer compare interrupt behavior.

```bash
./tiny16 run examples/timer.asm
```

**Expected Output:**

```
STimer
```

---

### 3. **Fibonacci Example**

This program computes the first 10 Fibonacci numbers and stores them in memory.

#### Assemble the program:

```bash
./tiny16 asm examples/fib.asm -o fib.bin
```

#### Emulate the binary starting at address `0x0100`:

```bash
./tiny16 emu fib.bin --base 0x0000 --pc 0x0100 --dump 0x0100 0x0140
```

You will see a memory dump containing the first 10 Fibonacci numbers in 16-bit words.

---

## 🧠 Memory-Mapped I/O (MMIO)

Tiny16 supports:

### **UART Output (0xFF00)**
Writing a byte prints it to stdout.

### **Timer Registers**
| Address    | Register      | Description |
|------------|---------------|-------------|
| `0xFF10`   | TIMER (low)   | Current timer value (low byte) |
| `0xFF11`   | TIMER (high)  | Current timer value (high byte) |
| `0xFF12`   | TIMERCMP (low) | Timer compare register (low byte) |
| `0xFF13`   | TIMERCMP (high)| Timer compare register (high byte) |
| `0xFF14`   | IRQ Pending Flag | 1 when timer matches compare |

The emulator increments TIMER after each instruction and sets the IRQ flag when:

```
TIMER == TIMERCMP
```

---

## 👥 Team Members

- Abdul Muqtadir Mohammed  
- Akash Kishorbhai Devani  
- Faisal Barkatali Budhwani  
- Venkata Sai Anjana Karthikeya Nimmala Sri Naga  

---

## 📄 License

This project is for academic use only (CMPE 220 – System Software FA25).

---
