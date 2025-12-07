# Tiny16 Software CPU & Program Layout/Execution  
**CMPE 220 – System Software (FA25)**  

This repository contains two related assignments:

1. **Tiny16 – Software CPU Design**  
2. **Program Layout & Execution – Recursion, Call Stack, and Memory Layout**

---

## 🚀 Project Overview

Tiny16 is a fully software-implemented 16-bit CPU written in C++.  
Features include:

- CPU core (registers, ALU, condition flags)
- Instruction Set Architecture (ISA)
- Memory + MMIO (UART + Timer)
- Two-pass assembler
- Emulator with memory dump support
- Example programs:
  - `hello.asm`
  - `timer.asm`
  - `fib.asm`
  - `fact.asm` (recursion assignment)

---

## 📂 Project Structure

```
software_cpu_design/
│
├── tiny16.cpp
├── factorial.c
├── Factorial Demo.mp4
├── Sample Drawing.png
├── examples/
│   ├── fact.asm
│   ├── hello.asm
│   ├── timer.asm
│   └── fib.asm
└── README.md
```

---

# 🔧 Requirements

- macOS or Linux  
- `g++` with C++17 support  
- Terminal or shell environment  

---

# 🛠 Compilation

Compile Tiny16:

```bash
g++ -std=c++17 -O2 -o tiny16 tiny16.cpp
```

This produces the executable `tiny16`.

---

# ▶️ Running Example Programs

## 1. Hello World

```bash
./tiny16 run examples/hello.asm
```

Expected:

```
Hello, World!
```

---

## 2. Timer Example

```bash
./tiny16 run examples/timer.asm
```

Expected:

```
STimer
```

---

## 3. Fibonacci Example

### Assemble:

```bash
./tiny16 asm examples/fib.asm -o fib.bin
```

### Emulate:

```bash
./tiny16 emu fib.bin --base 0x0000 --pc 0x0100 --dump 0x0100 0x0140
```

---

# 🧠 Memory-Mapped I/O (MMIO)

| Address | Register           | Description |
|--------|--------------------|-------------|
| 0xFF10 | TIMER (low)        | Timer low byte |
| 0xFF11 | TIMER (high)       | Timer high byte |
| 0xFF12 | TIMERCMP (low)     | Compare low byte |
| 0xFF13 | TIMERCMP (high)    | Compare high byte |
| 0xFF14 | IRQ Pending Flag   | Set to 1 when timer matches compare |

---

# 🔥 Program Layout & Execution (Recursion Assignment)

This portion of the project demonstrates recursion, stack behavior, and memory layout on both C and Tiny16.

---

## 📌 C Recursive Factorial

Source: `factorial.c`

Compile:

```bash
gcc -std=c11 -O2 -o factorial factorial.c
```

Run:

```bash
./factorial
```

Example:

```
Enter a number: 5
Factorial of 5 = 120
```

---

## 📌 Tiny16 Recursive Factorial (Assembly)

Assembly file: `examples/fact.asm`

Run directly:

```bash
./tiny16 run examples/fact.asm}
```

Demonstrates:

- CALL / RET  
- Stack-frame creation  
- Recursion expansion  
- Stack unwinding  
- Returning values  

---

## 📌 Examine Memory Layout

Assemble:

```bash
./tiny16 asm examples/fact.asm -o fact.bin
```

Emulate with dump:

```bash
./tiny16 emu fact.bin --base 0x0000 --pc 0x0000 --dump 0x0000 0x01FF
```

Shows:

- Code section  
- Data section  
- Stack frames  
- Return addresses  

---

# 🎥 Recursion Video

Included in the repo:

```
Factorial Demo.mp4
```

Covers:

- Tiny16 stack behavior  
- Function calls  
- Base case handling  
- Recursion depth  
- Stack unwinding  

---

# 👥 Team Members

- Abdul Muqtadir Mohammed  
- Akash Kishorbhai Devani  
- Faisal Barkatali Budhwani  
- Venkata Sai Anjana Karthikeya Nimmala Sri Naga  

---

# 📄 License

Academic use only — CMPE 220 (FA25).
