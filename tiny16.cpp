// tiny16.cpp
// A simple 16-bit software CPU with ISA, emulator, assembler, and example programs.

#include <iostream>
#include <vector>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <iterator>

using namespace std;

// ------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------
// Note: Helper functions removed as they were unused
// static inline uint16_t u16(int v){ return (uint16_t)(v & 0xFFFF); }
// static inline int16_t  s16(uint16_t v){ return (int16_t)v; }
// static inline uint8_t  u8 (int v){ return (uint8_t)(v & 0xFF); }

// ------------------------------------------------------------
// Memory with MMIO
// ------------------------------------------------------------
struct Memory {
    static constexpr size_t SIZE = 65536;
    array<uint8_t, SIZE> mem{};
    uint16_t timer = 0;
    uint16_t timercmp = 0;
    bool irq_pending = false;

    uint8_t mmio_read(uint16_t addr){
        switch(addr){
            case 0xFF00: // UART_OUT read (unused)
                return 0;
            case 0xFF01: // UART_IN hi byte (we just simulate "no data": 0xFFFF)
                return 0xFF;
            case 0xFF10: // TIMER low
                return (uint8_t)(timer & 0xFF);
            case 0xFF11: // TIMER high
                return (uint8_t)((timer >> 8) & 0xFF);
            case 0xFF12: // TIMERCMP low
                return (uint8_t)(timercmp & 0xFF);
            case 0xFF13: // TIMERCMP high
                return (uint8_t)((timercmp >> 8) & 0xFF);
            case 0xFF14: // IRQ pending flag
                return irq_pending ? 1 : 0;
            default:
                return 0;
        }
    }

    void mmio_write(uint16_t addr, uint8_t val){
        switch(addr){
            case 0xFF00: { // UART_OUT
                char ch = (char)val;
                cout << ch << flush;
                break;
            }
            case 0xFF10: // TIMER low
                timer = (uint16_t)((timer & 0xFF00) | val);
                break;
            case 0xFF11: // TIMER high
                timer = (uint16_t)((timer & 0x00FF) | (val << 8));
                break;
            case 0xFF12: // TIMERCMP low
                timercmp = (uint16_t)((timercmp & 0xFF00) | val);
                break;
            case 0xFF13: // TIMERCMP high
                timercmp = (uint16_t)((timercmp & 0x00FF) | (val << 8));
                break;
            case 0xFF14: // IRQ_ACK
                if (val == 1) irq_pending = false;
                break;
            default:
                // ignore unknown MMIO writes
                break;
        }
    }

    uint8_t read8(uint16_t addr){
        if (addr >= 0xFF00) return mmio_read(addr);
        return mem[addr];
    }

    uint16_t read16(uint16_t addr){
        uint16_t lo = read8(addr);
        uint16_t hi = read8(addr + 1);
        return (uint16_t)((hi << 8) | lo);
    }

    void write8(uint16_t addr, uint8_t val){
        if (addr >= 0xFF00) { mmio_write(addr, val); return; }
        mem[addr] = val;
    }

    void write16(uint16_t addr, uint16_t val){
        write8(addr, (uint8_t)(val & 0xFF));
        write8(addr + 1, (uint8_t)((val >> 8) & 0xFF));
    }

    void tick(){
        timer = (uint16_t)(timer + 1);
        // Set IRQ when timer reaches or exceeds compare value
        // Note: timercmp of 0 means "never trigger" (timer starts at 0, so 0 means disabled)
        if (timercmp > 0 && timer >= timercmp) {
            irq_pending = true;
        }
    }
};

// ------------------------------------------------------------
// CPU core
// ------------------------------------------------------------
struct CPU {
    Memory &mem;
    uint16_t R[8]{}; // R0..R7, R7 = stack pointer
    uint16_t PC = 0;
    bool Z = false, N = false, C = false, V = false;
    bool halted = false;

    CPU(Memory &m) : mem(m) {
        R[7] = 0x7FFC; // stack
    }

    uint16_t fetch16(){
        uint16_t w = mem.read16(PC);
        PC = (uint16_t)(PC + 2);
        return w;
    }

    void setZN(uint16_t res){
        Z = (res == 0);
        N = ((res & 0x8000) != 0);
    }

    void add16(uint16_t a, uint16_t b, uint16_t &res){
        uint32_t w = (uint32_t)a + (uint32_t)b;
        res = (uint16_t)w;
        C = (w >> 16) & 1;
        V = ((~(a ^ b) & (a ^ res)) >> 15) & 1;
        setZN(res);
    }

    void sub16(uint16_t a, uint16_t b, uint16_t &res){
        uint32_t w = (uint32_t)a + (uint32_t)(~b) + 1;
        res = (uint16_t)w;
        C = (w >> 16) & 1; // carry = !borrow
        V = (((a ^ b) & (a ^ res)) >> 15) & 1;
        setZN(res);
    }

    void push16(uint16_t v){
        R[7] = (uint16_t)(R[7] - 2);
        mem.write16(R[7], v);
    }

    uint16_t pop16(){
        uint16_t v = mem.read16(R[7]);
        R[7] = (uint16_t)(R[7] + 2);
        return v;
    }

    void exec(){
        if (halted) return;

        uint16_t insn = fetch16();
        uint8_t opcode = (insn >> 11) & 0x1F;
        uint8_t rd     = (insn >> 8)  & 0x07;
        uint8_t rs1    = (insn >> 5)  & 0x07;
        uint8_t imm3   = insn & 0x07;
        uint8_t imm8   = insn & 0xFF;
        int8_t  simm8  = (int8_t)imm8;

        auto wide = [&](uint16_t &w){ w = fetch16(); };

        switch (opcode){
            case 0x00: // NOP
                break;
            case 0x01: // HALT
                halted = true;
                break;

            case 0x02: { // LDI rd, imm16
                uint16_t w; wide(w);
                R[rd] = w;
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x03: { // MOV rd, rs1
                R[rd] = R[rs1];
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x04: { // ADD rd, rs1
                uint16_t r; add16(R[rd], R[rs1], r);
                R[rd] = r;
                break;
            }

            case 0x05: { // SUB rd, rs1
                uint16_t r; sub16(R[rd], R[rs1], r);
                R[rd] = r;
                break;
            }

            case 0x06: { // AND rd, rs1
                R[rd] &= R[rs1];
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x07: { // OR rd, rs1
                R[rd] |= R[rs1];
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x08: { // XOR rd, rs1
                R[rd] ^= R[rs1];
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x09: { // NOT rd
                R[rd] = ~R[rd];
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x0A: { // SHL rd, imm3
                uint8_t sh = imm3 & 7;
                if (sh){
                    C = (R[rd] >> (16 - sh)) & 1;
                    R[rd] <<= sh;
                } else {
                    C = 0;
                }
                setZN(R[rd]);
                V = 0;
                break;
            }

            case 0x0B: { // SHR rd, imm3 (logical)
                uint8_t sh = imm3 & 7;
                if (sh){
                    C = (R[rd] >> (sh - 1)) & 1;
                    R[rd] >>= sh;
                } else {
                    C = 0;
                }
                setZN(R[rd]);
                V = 0;
                break;
            }

            case 0x0C: { // ADDI rd, imm8 (sign-extended)
                uint16_t r; add16(R[rd], (uint16_t)(int16_t)simm8, r);
                R[rd] = r;
                break;
            }

            case 0x0D: { // CMPI rd, imm8
                uint16_t r; sub16(R[rd], (uint16_t)(int16_t)simm8, r);
                break;
            }

            case 0x0E: { // CMP rd, rs1
                uint16_t r; sub16(R[rd], R[rs1], r);
                break;
            }

            case 0x0F: { // LD rd, [addr16]
                uint16_t addr; wide(addr);
                R[rd] = mem.read16(addr);
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x10: { // ST rs1, [addr16]
                uint16_t addr; wide(addr);
                mem.write16(addr, R[rs1]);
                break;
            }

            case 0x11: { // LDB rd, [addr16]
                uint16_t addr; wide(addr);
                R[rd] = mem.read8(addr);
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x12: { // STB rs1, [addr16]
                uint16_t addr; wide(addr);
                mem.write8(addr, (uint8_t)(R[rs1] & 0xFF));
                break;
            }

            case 0x13: { // LD rd, [rb+imm5]
                int8_t simm5 = (int8_t)(insn & 0x1F);
                if (simm5 & 0x10) simm5 |= ~0x1F; // sign extend 5-bit
                uint16_t addr = (uint16_t)(R[rs1] + (int16_t)simm5);
                R[rd] = mem.read16(addr);
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x14: { // ST rs1, [rb+imm5] (rb=rd)
                int8_t simm5 = (int8_t)(insn & 0x1F);
                if (simm5 & 0x10) simm5 |= ~0x1F;
                uint16_t addr = (uint16_t)(R[rd] + (int16_t)simm5);
                mem.write16(addr, R[rs1]);
                break;
            }

            case 0x15: { // JMP addr16
                uint16_t a; wide(a);
                PC = a;
                break;
            }

            case 0x16: { // JZ addr16
                uint16_t a; wide(a);
                if (Z) PC = a;
                break;
            }

            case 0x17: { // JNZ addr16
                uint16_t a; wide(a);
                if (!Z) PC = a;
                break;
            }

            case 0x18: { // JC addr16
                uint16_t a; wide(a);
                if (C) PC = a;
                break;
            }

            case 0x19: { // JN addr16
                uint16_t a; wide(a);
                if (N) PC = a;
                break;
            }

            case 0x1A: { // CALL addr16
                uint16_t a; wide(a);
                push16(PC);
                PC = a;
                break;
            }

            case 0x1B: { // RET
                PC = pop16();
                break;
            }

            case 0x1C: { // IN rd, [io_addr]
                uint16_t a; wide(a);
                // For MMIO addresses, read as byte and zero-extend
                if (a >= 0xFF00) {
                    R[rd] = mem.read8(a);
                } else {
                    R[rd] = mem.read16(a);
                }
                setZN(R[rd]);
                C = V = 0;
                break;
            }

            case 0x1D: { // OUT rs1, [io_addr]
                uint16_t a; wide(a);
                // For MMIO addresses, write as byte (UART expects byte)
                if (a >= 0xFF00) {
                    mem.write8(a, (uint8_t)(R[rs1] & 0xFF));
                } else {
                    mem.write16(a, R[rs1]);
                }
                break;
            }

            default:
                cerr << "Unknown opcode: " << (int)opcode
                     << " at PC=0x" << hex << (PC - 2) << dec << "\n";
                halted = true;
                break;
        }

        mem.tick();
    }
};

// ------------------------------------------------------------
// Assembler
// ------------------------------------------------------------
struct Assembler {
    unordered_map<string,uint16_t> sym;   // symbol table
    vector<uint8_t> bytes;               // output
    vector<pair<int,string>> fixups;     // (offset, symbol)
    uint16_t org = 0x0000;
    vector<string> lines;

    static string lower(const string& s){
        string r = s;
        for (auto &c : r) c = (char)tolower(c);
        return r;
    }

    static bool is_space(char c){
        return c==' ' || c=='\t' || c=='\r' || c=='\n';
    }

    static string trim(const string& s){
        size_t a = 0, b = s.size();
        while (a < b && is_space(s[a])) a++;
        while (b > a && is_space(s[b-1])) b--;
        return s.substr(a, b - a);
    }

    static vector<string> splitComma(const string& s){
        vector<string> out;
        string cur;
        int par = 0;
        bool inStr = false;
        for(char c : s){
            if (c == '"') inStr = !inStr;
            if (!inStr && c == '[') par++;
            if (!inStr && c == ']') par--;
            if (!inStr && par == 0 && c == ','){
                out.push_back(trim(cur));
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) out.push_back(trim(cur));
        return out;
    }

    static bool parse_reg(const string& s, int &r){
        string t = lower(s);
        if (t.size() < 2 || t[0] != 'r') return false;
        char *end = nullptr;
        long v = strtol(t.c_str() + 1, &end, 10);
        if (*end != 0) return false;
        if (v < 0 || v > 7) return false;
        r = (int)v;
        return true;
    }

    static bool parse_int(const string& s, int &v){
        string t = s;
        if (!t.empty() && t[0] == '#') t = t.substr(1);

        // char literal: 'A' or '\n'
        if (t.size() >= 3 && t.front()=='\'' && t.back()=='\''){
            if (t.size() == 3){
                v = (unsigned char)t[1];
                return true;
            }
            if (t.size() == 4 && t[1] == '\\'){
                if (t[2]=='n') { v = '\n'; return true; }
                if (t[2]=='t') { v = '\t'; return true; }
                if (t[2]=='0') { v = '\0'; return true; }
                v = (unsigned char)t[2];
                return true;
            }
        }

        int base = 10;
        const char *c = t.c_str();
        if (t.size() > 2 && t[0]=='0' && (t[1]=='x' || t[1]=='X')) base = 16;
        char *end = nullptr;
        long L = strtol(c, &end, base);
        if (*end == 0){ v = (int)L; return true; }
        return false;
    }

    void load(const string& text){
        lines.clear();
        sym.clear();
        bytes.clear();
        fixups.clear();
        org = 0;

        string cur;
        stringstream ss(text);
        while (getline(ss, cur)){
            lines.push_back(cur);
        }
    }

    void emit8(uint8_t b){ bytes.push_back(b); }
    void emit16(uint16_t w){
        emit8((uint8_t)(w & 0xFF));
        emit8((uint8_t)((w >> 8) & 0xFF));
    }

    void emit_fixup16(const string& name){
        fixups.push_back({(int)bytes.size(), name});
        emit16(0);
    }

    uint16_t reg_of(const string& tok){
        int r;
        if (!parse_reg(tok, r)) throw runtime_error("bad register: " + tok);
        return (uint16_t)r;
    }

    // [0x1234] or [label]
    static bool parse_addr_token(const string& tok, uint16_t &addr, string &symb){
        string t = trim(tok);
        if (t.size() < 3 || t.front() != '[' || t.back() != ']') return false;
        string inner = trim(t.substr(1, t.size()-2));
        int v;
        if (parse_int(inner, v)){
            addr = (uint16_t)v;
            symb.clear();
            return true;
        }
        symb = lower(inner);
        return true;
    }

    // ----------------- Pass 1: build symbol table & size -----------------
    void pass1(){
        uint16_t pc = org;

        for (auto raw : lines){

            // strip comments
            string s = raw;
            size_t sc = s.find(';');
            if (sc != string::npos) s = s.substr(0, sc);
            s = trim(s);
            if (s.empty()) continue;

            // pure label
            if (s.back() == ':'){
                string lab = trim(s.substr(0, s.size()-1));
                sym[lower(lab)] = pc;
                continue;
            }

            // label + rest
            size_t colon = s.find(':');
            if (colon != string::npos){
                string lab = trim(s.substr(0, colon));
                sym[lower(lab)] = pc;
                s = trim(s.substr(colon+1));
                if (s.empty()) continue;
            }

            string low = lower(s);

            // directives
            if (low.rfind(".org", 0) == 0){
                int v;
                if (!parse_int(trim(s.substr(4)), v))
                    throw runtime_error(".org expects value");
                pc = (uint16_t)v;
                continue;
            }

            if (low.rfind(".word", 0) == 0){
                string rest = trim(s.substr(5));
                auto parts = splitComma(rest);
                for (size_t i = 0; i < parts.size(); ++i){
                    pc += 2;
                }
                continue;
            }

            if (low.rfind(".stringz", 0) == 0){
                string rest = trim(s.substr(8));
                if (rest.empty() || rest[0] != '"')
                    throw runtime_error(".stringz expects string");
                string body;
                bool esc = false;
                for (size_t i = 1; i < rest.size(); ++i){
                    char c = rest[i];
                    if (esc){
                        if (c=='n') body.push_back('\n');
                        else if (c=='t') body.push_back('\t');
                        else if (c=='0') body.push_back('\0');
                        else body.push_back(c);
                        esc = false;
                    } else {
                        if (c=='\\') esc = true;
                        else if (c=='"') break;
                        else body.push_back(c);
                    }
                }
                pc = (uint16_t)(pc + (uint16_t)(body.size() + 1));
                continue;
            }

            // instruction size estimation
            string mnemRaw, rest;
            {
                stringstream ts(s);
                ts >> mnemRaw;
                getline(ts, rest);
            }
            string mnem = lower(mnemRaw);
            rest = trim(rest);

            auto needWide = [&](const string& m)->bool{
                static unordered_set<string> w = {
                    "ldi","ldb","stb","jmp","jz","jnz","jc","jn",
                    "call","in","out"
                };
                return w.count(m) > 0;
            };

            if (mnem == "ld" || mnem == "st"){
                // detect short ([rb+imm]) vs absolute ([addr])
                auto parts = splitComma(rest);
                if (parts.size() == 2 && parts[1].find('+') != string::npos){
                    pc = (uint16_t)(pc + 2); // short form: 1 word
                } else {
                    pc = (uint16_t)(pc + 4); // absolute: 2-word
                }
            } else {
                pc = (uint16_t)(pc + 2);
                if (needWide(mnem)) pc = (uint16_t)(pc + 2);
            }
        }
    }

    // ----------------- Pass 2: emit code -----------------
    void pass2(){
        uint16_t pc = org;
        bytes.clear();

        for (auto raw : lines){

            // strip comments
            string s = raw;
            size_t sc = s.find(';');
            if (sc != string::npos) s = s.substr(0, sc);
            s = trim(s);
            if (s.empty()) continue;

            if (s.back() == ':') continue; // pure label

            size_t colon = s.find(':');
            if (colon != string::npos){
                s = trim(s.substr(colon + 1));
                if (s.empty()) continue;
            }

            string low = lower(s);

            // directives
            if (low.rfind(".org", 0) == 0){
                int v;
                parse_int(trim(s.substr(4)), v);
                pc = (uint16_t)v;
                while (bytes.size() < (size_t)(pc - org))
                    bytes.push_back(0);
                continue;
            }

            if (low.rfind(".word", 0) == 0){
                string rest = trim(s.substr(5));
                auto parts = splitComma(rest);
                for (auto &p : parts){
                    int v;
                    if (parse_int(p, v)) emit16((uint16_t)v);
                    else emit_fixup16(lower(trim(p)));
                }
                continue;
            }

            if (low.rfind(".stringz", 0) == 0){
                string rest = trim(s.substr(8));
                string body;
                bool esc = false;
                for (size_t i = 1; i < rest.size(); ++i){
                    char c = rest[i];
                    if (esc){
                        if (c=='n') body.push_back('\n');
                        else if (c=='t') body.push_back('\t');
                        else if (c=='0') body.push_back('\0');
                        else body.push_back(c);
                        esc = false;
                    } else {
                        if (c=='\\') esc = true;
                        else if (c=='"') break;
                        else body.push_back(c);
                    }
                }
                for (char c : body) emit8((uint8_t)c);
                emit8(0);
                continue;
            }

            // instructions
            string mnemRaw, rest;
            {
                stringstream ts(s);
                ts >> mnemRaw;
                getline(ts, rest);
            }
            string M = lower(mnemRaw);
            rest = trim(rest);
            auto parts = splitComma(rest);

            auto emitRRI = [&](uint8_t op, uint8_t rd, uint8_t rs1, uint8_t imm3){
                uint16_t w = (uint16_t)((op << 11) | (rd << 8) | (rs1 << 5) | (imm3 & 0x07));
                emit16(w);
            };
            auto emitR = [&](uint8_t op, uint8_t rd, uint8_t rs1){
                uint16_t w = (uint16_t)((op << 11) | (rd << 8) | (rs1 << 5));
                emit16(w);
            };
            auto emitRI8 = [&](uint8_t op, uint8_t rd, int imm8){
                uint16_t w = (uint16_t)((op << 11) | (rd << 8) | ((uint8_t)imm8));
                emit16(w);
            };
            auto emitW = [&](uint8_t op, uint8_t rd){
                uint16_t w = (uint16_t)((op << 11) | (rd << 8));
                emit16(w);
            };

            if (M == "nop"){
                emit16(0x0000);
                continue;
            }
            if (M == "halt"){
                emit16((uint16_t)((0x01 << 11)));
                continue;
            }
            if (M == "ldi"){
                if (parts.size() != 2) throw runtime_error("LDI rd, imm16");
                int rd = reg_of(parts[0]);
                emitW(0x02, (uint8_t)rd);
                int v;
                if (parse_int(parts[1], v)) emit16((uint16_t)v);
                else emit_fixup16(lower(parts[1]));
                continue;
            }
            if (M == "mov"){
                if (parts.size() != 2) throw runtime_error("MOV rd, rs");
                int rd = reg_of(parts[0]);
                int rs = reg_of(parts[1]);
                emitR(0x03, (uint8_t)rd, (uint8_t)rs);
                continue;
            }
            if (M == "add"){
                int rd = reg_of(parts[0]);
                int rs = reg_of(parts[1]);
                emitR(0x04, (uint8_t)rd, (uint8_t)rs);
                continue;
            }
            if (M == "sub"){
                int rd = reg_of(parts[0]);
                int rs = reg_of(parts[1]);
                emitR(0x05, (uint8_t)rd, (uint8_t)rs);
                continue;
            }
            if (M == "and"){
                int rd = reg_of(parts[0]);
                int rs = reg_of(parts[1]);
                emitR(0x06, (uint8_t)rd, (uint8_t)rs);
                continue;
            }
            if (M == "or"){
                int rd = reg_of(parts[0]);
                int rs = reg_of(parts[1]);
                emitR(0x07, (uint8_t)rd, (uint8_t)rs);
                continue;
            }
            if (M == "xor"){
                int rd = reg_of(parts[0]);
                int rs = reg_of(parts[1]);
                emitR(0x08, (uint8_t)rd, (uint8_t)rs);
                continue;
            }
            if (M == "not"){
                if (parts.size() != 1) throw runtime_error("NOT rd");
                int rd = reg_of(parts[0]);
                emitR(0x09, (uint8_t)rd, 0);
                continue;
            }
            if (M == "shl"){
                int rd = reg_of(parts[0]);
                int s;
                if (!parse_int(parts[1], s) || s < 0 || s > 7)
                    throw runtime_error("SHL rd, 0..7");
                emitRRI(0x0A, (uint8_t)rd, 0, (uint8_t)s);
                continue;
            }
            if (M == "shr"){
                int rd = reg_of(parts[0]);
                int s;
                if (!parse_int(parts[1], s) || s < 0 || s > 7)
                    throw runtime_error("SHR rd, 0..7");
                emitRRI(0x0B, (uint8_t)rd, 0, (uint8_t)s);
                continue;
            }
            if (M == "addi"){
                int rd = reg_of(parts[0]);
                int v;
                if (!parse_int(parts[1], v)) throw runtime_error("ADDI rd, imm8");
                emitRI8(0x0C, (uint8_t)rd, v);
                continue;
            }
            if (M == "cmpi"){
                int rd = reg_of(parts[0]);
                int v;
                if (!parse_int(parts[1], v)) throw runtime_error("CMPI rd, imm8");
                emitRI8(0x0D, (uint8_t)rd, v);
                continue;
            }
            if (M == "cmp"){
                int rd = reg_of(parts[0]);
                int rs = reg_of(parts[1]);
                emitR(0x0E, (uint8_t)rd, (uint8_t)rs);
                continue;
            }

            if (M == "ld"){
                if (parts.size() != 2) throw runtime_error("LD rd, [..]");
                int rd = reg_of(parts[0]);
                string addrTok = parts[1];

                if (addrTok.find('+') != string::npos){
                    // short form: LD rd, [rb+imm5]
                    // encode: op=0x13, bits 15..11 op, 10..8 rd, 7..5 rb, 4..0 imm5
                    string inside = trim(addrTok.substr(1, addrTok.size()-2)); // rb+imm
                    auto plus = inside.find('+');
                    if (plus == string::npos) throw runtime_error("LD short expects [rb+imm]");
                    string rbS = trim(inside.substr(0, plus));
                    string immS = trim(inside.substr(plus + 1));
                    int rb = reg_of(rbS);
                    int v;
                    if (!parse_int(immS, v)) throw runtime_error("LD short imm must be int");
                    int imm5 = v & 0x1F;
                    uint16_t w = (uint16_t)((0x13 << 11) | (rd << 8) | (rb << 5) | imm5);
                    emit16(w);
                } else {
                    // absolute: LD rd, [addr16]
                    emitW(0x0F, (uint8_t)rd);
                    uint16_t addr;
                    string symb;
                    if (parse_addr_token(addrTok, addr, symb)){
                        if (symb.empty()) emit16(addr);
                        else emit_fixup16(symb);
                    } else {
                        throw runtime_error("LD rd, [addr16]");
                    }
                }
                continue;
            }

            if (M == "st"){
                if (parts.size() != 2) throw runtime_error("ST rs, [..]");
                int rs = reg_of(parts[0]);
                string addrTok = parts[1];

                if (addrTok.find('+') != string::npos){
                    // short form: ST rs, [rb+imm5], encoded as op=0x14 rd=rb, rs1=rs
                    string inside = trim(addrTok.substr(1, addrTok.size()-2));
                    auto plus = inside.find('+');
                    if (plus == string::npos) throw runtime_error("ST short expects [rb+imm]");
                    string rbS = trim(inside.substr(0, plus));
                    string immS = trim(inside.substr(plus + 1));
                    int rb = reg_of(rbS);
                    int v;
                    if (!parse_int(immS, v)) throw runtime_error("ST short imm must be int");
                    int imm5 = v & 0x1F;
                    uint16_t w = (uint16_t)((0x14 << 11) | (rb << 8) | (rs << 5) | imm5);
                    emit16(w);
                } else {
                    emitW(0x10, (uint8_t)rs);
                    uint16_t addr;
                    string symb;
                    if (parse_addr_token(addrTok, addr, symb)){
                        if (symb.empty()) emit16(addr);
                        else emit_fixup16(symb);
                    } else {
                        throw runtime_error("ST rs, [addr16]");
                    }
                }
                continue;
            }

            if (M == "ldb"){
                if (parts.size() != 2) throw runtime_error("LDB rd, [addr16]");
                int rd = reg_of(parts[0]);
                emitW(0x11, (uint8_t)rd);
                uint16_t addr;
                string symb;
                if (parse_addr_token(parts[1], addr, symb)){
                    if (symb.empty()) emit16(addr);
                    else emit_fixup16(symb);
                } else {
                    throw runtime_error("LDB rd, [addr16]");
                }
                continue;
            }

            if (M == "stb"){
                if (parts.size() != 2) throw runtime_error("STB rs, [addr16]");
                int rs = reg_of(parts[0]);
                emitW(0x12, (uint8_t)rs);
                uint16_t addr;
                string symb;
                if (parse_addr_token(parts[1], addr, symb)){
                    if (symb.empty()) emit16(addr);
                    else emit_fixup16(symb);
                } else {
                    throw runtime_error("STB rs, [addr16]");
                }
                continue;
            }

            if (M == "jmp" || M == "jz" || M == "jnz" || M == "jc" || M == "jn"){
                uint8_t op =
                    (M == "jmp") ? 0x15 :
                    (M == "jz")  ? 0x16 :
                    (M == "jnz") ? 0x17 :
                    (M == "jc")  ? 0x18 : 0x19;
                emitW(op, 0);
                int v;
                if (parse_int(parts[0], v)) emit16((uint16_t)v);
                else emit_fixup16(lower(parts[0]));
                continue;
            }

            if (M == "call"){
                emitW(0x1A, 0);
                int v;
                if (parse_int(parts[0], v)) emit16((uint16_t)v);
                else emit_fixup16(lower(parts[0]));
                continue;
            }

            if (M == "ret"){
                emitW(0x1B, 0);
                continue;
            }

            if (M == "in"){
                if (parts.size() != 2) throw runtime_error("IN rd, [addr16]");
                int rd = reg_of(parts[0]);
                emitW(0x1C, (uint8_t)rd);
                uint16_t addr;
                string symb;
                if (parse_addr_token(parts[1], addr, symb)){
                    if (symb.empty()) emit16(addr);
                    else emit_fixup16(symb);
                } else {
                    throw runtime_error("IN rd, [addr16]");
                }
                continue;
            }

            if (M == "out"){
                if (parts.size() != 2) throw runtime_error("OUT rs, [addr16]");
                int rs = reg_of(parts[0]);
                // OUT format: opcode=0x1D, rd=0 (unused), rs1=rs
                uint16_t w = (uint16_t)((0x1D << 11) | (0 << 8) | (rs << 5));
                emit16(w);
                uint16_t addr;
                string symb;
                if (parse_addr_token(parts[1], addr, symb)){
                    if (symb.empty()) emit16(addr);
                    else emit_fixup16(symb);
                } else {
                    throw runtime_error("OUT rs, [addr16]");
                }
                continue;
            }

            throw runtime_error(string("Unknown mnemonic: ") + M);
        }

        // resolve fixups
        for (auto &fx : fixups){
            int off = fx.first;
            auto it = sym.find(fx.second);
            if (it == sym.end())
                throw runtime_error("undefined label: " + fx.second);
            uint16_t a = it->second;
            bytes[off]     = (uint8_t)(a & 0xFF);
            bytes[off + 1] = (uint8_t)((a >> 8) & 0xFF);
        }
    }
};

// ------------------------------------------------------------
// Example programs
// ------------------------------------------------------------

static const char *EX_HELLO = R"ASM(
; Minimal Hello, World using UART_OUT at 0xFF00
; No data section, no addressing tricks – just immediates.

.org 0x0000
start:
  ; "Hello, World!\n"
  LDI r0, 72      ; 'H'
  OUT r0, [0xFF00]

  LDI r0, 101     ; 'e'
  OUT r0, [0xFF00]

  LDI r0, 108     ; 'l'
  OUT r0, [0xFF00]

  LDI r0, 108     ; 'l'
  OUT r0, [0xFF00]

  LDI r0, 111     ; 'o'
  OUT r0, [0xFF00]

  LDI r0, 44      ; ','
  OUT r0, [0xFF00]

  LDI r0, 32      ; ' '
  OUT r0, [0xFF00]

  LDI r0, 87      ; 'W'
  OUT r0, [0xFF00]

  LDI r0, 111     ; 'o'
  OUT r0, [0xFF00]

  LDI r0, 114     ; 'r'
  OUT r0, [0xFF00]

  LDI r0, 108     ; 'l'
  OUT r0, [0xFF00]

  LDI r0, 100     ; 'd'
  OUT r0, [0xFF00]

  LDI r0, 33      ; '!'
  OUT r0, [0xFF00]

  LDI r0, 10      ; '\n'
  OUT r0, [0xFF00]

  HALT
)ASM";

static const char *EX_FIB = R"ASM(
; Fibonacci: compute first 10 16-bit Fibonacci numbers into memory
; at label 'buf' (you can inspect with --dump).

.org 0x0100
start:
  LDI r0, 0      ; a = 0
  LDI r1, 1      ; b = 1
  LDI r2, 10     ; count
  LDI r3, buf    ; pointer to buffer

loop:
  ST  r0, [r3+0] ; store a
  ADDI r3, #2    ; advance pointer (each word = 2 bytes)

  ; next fib
  MOV r4, r1     ; temp = b
  ADD r1, r0     ; b = a + b
  MOV r0, r4     ; a = old b

  ADDI r2, #-1
  JNZ loop

  HALT

buf:
  .word 0,0,0,0,0,0,0,0,0,0
)ASM";

static const char *EX_TIMER = R"ASM(
; Timer demo: demonstrates Fetch/Compute/Store cycles
; 
; This program demonstrates the Fetch/Compute/Store cycle by executing
; a series of instructions. Each instruction follows this cycle:
;
; Fetch/Compute/Store cycle:
; 1. Fetch: CPU fetches instruction from memory at Program Counter (PC)
; 2. Compute: ALU performs the operation (add, compare, load, etc.)
; 3. Store: Result is stored in register or memory
;
; The timer increments automatically after each instruction execution,
; demonstrating how many Fetch/Compute/Store cycles have occurred.

.org 0x0000
start:
  ; === Example 1: LDI (Load Immediate) - Fetch/Compute/Store ===
  ; Fetch: CPU fetches LDI opcode (0x02) from memory at PC
  ;        Then fetches immediate value 'S' (0x53) from next memory location
  ; Compute: ALU loads the immediate value 0x53 into the destination register
  ; Store: Value 0x53 is stored in register r3
  ; Timer increments: +2 (one for opcode fetch, one for immediate fetch)
  LDI r3, 83           ; Load 'S' (ASCII 83) into r3
  
  ; === Example 2: OUT (Output) - Fetch/Compute/Store ===
  ; Fetch: CPU fetches OUT opcode (0x1D) and address 0xFF00 from memory
  ; Compute: ALU gets value from r3 (83), computes MMIO address 0xFF00
  ; Store: Byte 83 is written to UART output register (prints 'S')
  ; Timer increments: +2 (one for opcode, one for address)
  OUT r3, [0xFF00]

  ; === Example 3: Arithmetic operations - Fetch/Compute/Store ===
  ; Demonstrates multiple Fetch/Compute/Store cycles
  LDI r0, 5            ; Fetch: LDI opcode+5, Compute: load 5, Store: to r0
  LDI r1, 3            ; Fetch: LDI opcode+3, Compute: load 3, Store: to r1
  ADD r0, r1           ; Fetch: ADD opcode, Compute: r0+r1=8, Store: to r0

  ; === Example 4: Print "Timer\n" - Multiple Fetch/Compute/Store cycles ===
  ; Each character print demonstrates a complete Fetch/Compute/Store cycle
  LDI r3, 84           ; 'T' - Fetch/Compute/Store
  OUT r3, [0xFF00]     ; Fetch/Compute/Store
  LDI r3, 105          ; 'i' - Fetch/Compute/Store
  OUT r3, [0xFF00]     ; Fetch/Compute/Store
  LDI r3, 109          ; 'm' - Fetch/Compute/Store
  OUT r3, [0xFF00]     ; Fetch/Compute/Store
  LDI r3, 101          ; 'e' - Fetch/Compute/Store
  OUT r3, [0xFF00]     ; Fetch/Compute/Store
  LDI r3, 114          ; 'r' - Fetch/Compute/Store
  OUT r3, [0xFF00]     ; Fetch/Compute/Store
  LDI r3, 10           ; '\n' - Fetch/Compute/Store
  OUT r3, [0xFF00]     ; Fetch/Compute/Store

  HALT
)ASM";

// in-memory "filesystem"
unordered_map<string,string> vfs = {
    {"examples/hello.asm", EX_HELLO},
    {"examples/fib.asm",   EX_FIB},
    {"examples/timer.asm", EX_TIMER}
};

string slurpFile(const string& path){
    auto it = vfs.find(path);
    if (it != vfs.end()) return it->second;

    ifstream f(path);
    if (!f.good()) throw runtime_error("Cannot open file: " + path);
    stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void saveBinary(const string& path, const vector<uint8_t>& bin){
    ofstream o(path, ios::binary);
    if (!o.good()) throw runtime_error("Cannot write: " + path);
    o.write((const char*)bin.data(), (streamsize)bin.size());
}

void loadImage(Memory &mem, const vector<uint8_t>& bin, uint16_t base){
    for (size_t i = 0; i < bin.size(); ++i)
        mem.write8((uint16_t)(base + i), bin[i]);
}

void dumpMemory(Memory &mem, uint16_t a0, uint16_t a1){
    for (uint32_t a = a0; a <= a1; a += 16){
        cout << hex << setw(4) << setfill('0') << a << ": ";
        for (int i = 0; i < 16 && a + i <= a1; ++i){
            cout << setw(2) << (int)mem.read8((uint16_t)(a + i)) << " ";
        }
        cout << dec << "\n";
    }
}

// ------------------------------------------------------------
// main
// ------------------------------------------------------------
int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if (argc < 2){
        cerr << "Usage: tiny16 <asm|emu|run> <file> [options]\n";
        cerr << "Examples:\n"
             << "  ./tiny16 run examples/hello.asm\n"
             << "  ./tiny16 run examples/timer.asm\n"
             << "  ./tiny16 asm examples/fib.asm -o fib.bin\n"
             << "  ./tiny16 emu fib.bin --base 0x0000 --pc 0x0100 --dump 0x0100 0x01FF\n";
        return 1;
    }

    string mode = argv[1];

    try {
        if (mode == "asm"){
            if (argc < 3) throw runtime_error("asm: missing <file>");
            string in = argv[2];
            string out = "a.bin";
            for (int i = 3; i < argc; ++i){
                string t = argv[i];
                if (t == "-o" && i + 1 < argc){
                    out = argv[++i];
                }
            }
            string text = slurpFile(in);
            Assembler A;
            A.load(text);
            A.pass1();
            A.pass2();
            saveBinary(out, A.bytes);
            cout << "Assembled " << in << " -> " << out
                 << " (" << A.bytes.size() << " bytes)\n";
        } else if (mode == "emu"){
            if (argc < 3) throw runtime_error("emu: missing <image.bin>");
            string img = argv[2];
            uint16_t base = 0x0000, pc = 0x0000;
            bool dump = false;
            uint16_t da = 0, db = 0;

            for (int i = 3; i < argc; ++i){
                string t = argv[i];
                if (t == "--base" && i + 1 < argc){
                    base = (uint16_t)strtoul(argv[++i], nullptr, 0);
                } else if (t == "--pc" && i + 1 < argc){
                    pc = (uint16_t)strtoul(argv[++i], nullptr, 0);
                } else if (t == "--dump" && i + 2 < argc){
                    dump = true;
                    da = (uint16_t)strtoul(argv[++i], nullptr, 0);
                    db = (uint16_t)strtoul(argv[++i], nullptr, 0);
                }
            }

            ifstream f(img, ios::binary);
            if (!f.good()) throw runtime_error("Cannot open image: " + img);
            vector<uint8_t> bin((istreambuf_iterator<char>(f)), {});

            Memory M;
            loadImage(M, bin, base);
            CPU cpu(M);
            cpu.PC = pc;
            while (!cpu.halted) cpu.exec();

            if (dump) dumpMemory(M, da, db);

        } else if (mode == "run"){
            if (argc < 3) throw runtime_error("run: missing <file.asm>");
            string in = argv[2];
            bool doDump = false;
            uint16_t da = 0, db = 0;
            for (int i = 3; i < argc; ++i){
                string t = argv[i];
                if (t == "--dump" && i + 2 < argc){
                    doDump = true;
                    da = (uint16_t)strtoul(argv[++i], nullptr, 0);
                    db = (uint16_t)strtoul(argv[++i], nullptr, 0);
                }
            }

            string text = slurpFile(in);
            Assembler A;
            A.load(text);
            A.pass1();
            A.pass2();

            Memory M;
            loadImage(M, A.bytes, 0x0000);
            CPU cpu(M);
            cpu.PC = 0x0000;
            while (!cpu.halted) cpu.exec();

            if (doDump) dumpMemory(M, da, db);
        } else {
            throw runtime_error("unknown mode: " + mode);
        }
    } catch (const exception& e){
        cerr << "Error: " << e.what() << "\n";
        return 2;
    }

    return 0;
}
