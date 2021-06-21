//
// Created by Matan Tennenhaus on 6/20/2021.
//

#include "elf64.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <syscall.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/reg.h>
#include <sys/user.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#define STB_LOCAL	(0)
#define STB_GLOBAL	(1)
#define SHT_SYMTAB  (2)

Elf64_Addr get_function_addr(char* func_name, char* file_name){

    FILE* file = fopen(file_name,"r");
    if (file == NULL){
        perror("fopen failed");
        return -1;
    }
    //get header
    Elf64_Ehdr child_header;
    if (fread((void*)&child_header, sizeof(child_header), 1, file) != 1){
        perror("fread 1 failed");
        return -1;
    }
    //get sections
    Elf64_Off sections_off = child_header.e_shoff;
    Elf64_Half section_size = child_header.e_shentsize;
    Elf64_Half sections_num = child_header.e_shnum;
    Elf64_Half	sections_header_names = child_header.e_shstrndx;
    fseek(file, sections_off, SEEK_SET);
    char sections[section_size * sections_num];
    if (fread((void*)&sections, section_size, sections_num, file) != sections_num){
        perror("fread 2 failed");
        return -1;
    }

    //get sections names
    Elf64_Shdr* shstrtab_entry = (Elf64_Shdr*)(sections + (sections_header_names * section_size));
    Elf64_Off shstrtab_offset = shstrtab_entry->sh_offset;
    Elf64_Xword shstrtab_size = shstrtab_entry->sh_size;
    char shstrtab[shstrtab_size];
    fseek(file, shstrtab_offset, SEEK_SET);
    if (fread((void*)&shstrtab, shstrtab_size, 1 ,file) != 1){
        perror("fread 3 failed");
        return -1;
    }

    //get symbol tables section header
    Elf64_Shdr *symtab_section = NULL;
    for (size_t i = 0; i < sections_num; ++i) {
        Elf64_Shdr* section = (Elf64_Shdr*)(sections + (i * section_size));
        if (strcmp(".symtab", shstrtab + section->sh_name) == 0){
        //if (section->sh_type == SHT_SYMTAB){
            symtab_section = section;
            break;
        }
    }
    if (symtab_section == NULL){
        perror("could not find symtab section");
        return -1;
    }
    Elf64_Off symtab_offset = symtab_section->sh_offset;
    Elf64_Xword symtab_size = symtab_section->sh_size;
    Elf64_Xword	symbol_entry_size = symtab_section->sh_entsize;
    Elf64_Word	symtab_names_section = symtab_section->sh_link;

    //get symbol table names section
    Elf64_Shdr* strtab_entry = (Elf64_Shdr*)(sections + (symtab_names_section * section_size));
    Elf64_Off strtab_offset = strtab_entry->sh_offset;
    Elf64_Xword strtab_size = strtab_entry->sh_size;
    char strtab[strtab_size];
    fseek(file, strtab_offset, SEEK_SET);
    if (fread((void*)&strtab, strtab_size, 1 ,file) != 1){
        perror("fread 4 failed");
        return -1;
    }

    //get symbols
    char symtab[symtab_size];
    fseek(file, symtab_offset, SEEK_SET);
    if (fread((void*)&symtab, symtab_size, 1 ,file) != 1){
        perror("fread 5 failed");
        return -1;
    }

    //find matching symbol to function
    Elf64_Sym* symbol = NULL;
    bool found = false;
    bool local = false;
    for (size_t i = 0; i < symtab_size; ++i) {
        symbol = (Elf64_Sym*)(symtab + (i * symbol_entry_size));
        //matching name is is in text (function) and not a variable
        Elf64_Shdr* section = (Elf64_Shdr*)(sections + (symbol->st_shndx * section_size));
        if (strcmp(func_name, strtab + symbol->st_name) == 0 &&
                strcmp(".text", shstrtab + section->sh_name) == 0){
            found = true;
            if (ELF64_ST_BIND(symbol->st_info) == STB_LOCAL)
                local = true;
            else
                break;
        }
    }
    fclose(file);
    if(!found) {
        printf("PRF:: not found!\n");
        return -1;
    }
    else if (local) {
        printf("PRF:: local found!\n");
        return -1;
    }
    else {
        //Elf64_Shdr *text_entry = (Elf64_Shdr *) (sections + (symbol->st_shndx * section_size));
        //return text_entry->sh_addr + symbol->st_value;
        return symbol->st_value;
    }
}
void run_track_syscalls_in_function(pid_t pid, Elf64_Addr function_addr) {
    int status;
    struct user_regs_struct regs;
    wait(&status);
    while (WIFSTOPPED(status)) {
        //trap int 3 into function entry
        long func_entry = ptrace(PTRACE_PEEKTEXT, pid, (void *) function_addr, NULL);
        long func_entry_trap = (func_entry & 0xFFFFFFFFFFFFFF00) | 0xCC;
        ptrace(PTRACE_POKETEXT, pid, (void *) function_addr, (void *) func_entry_trap);

        //let the child run to breakpoint or to exit the program (if no more function calls / there is none)
        ptrace(PTRACE_CONT, pid, NULL, NULL);
        wait(&status);
        if (WIFEXITED(status)) {
            return;
        }

        //remove the breakpoint in function entry
        ptrace(PTRACE_GETREGS, pid, 0, &regs);
        regs.rip -= 1;
        ptrace(PTRACE_SETREGS, pid, 0, &regs);
        ptrace(PTRACE_POKETEXT, pid, (void *) function_addr, (void *) func_entry);

        //trap function exit in memory value of rbp
        Elf64_Addr function_return_addr = ptrace(PTRACE_PEEKTEXT, pid, (void *) regs.rsp, NULL);
        long function_finish = ptrace(PTRACE_PEEKTEXT, pid, (void *) function_return_addr, NULL);
        Elf64_Addr function_finish_trap = (function_finish & 0xFFFFFFFFFFFFFF00) | 0xCC;
        ptrace(PTRACE_POKETEXT, pid, (void *) function_return_addr, (void *) function_finish_trap);

        //run until exiting function
        while (1) {
            ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
            wait(&status);
            ptrace(PTRACE_GETREGS, pid, 0, &regs);
            if (regs.rip - 1 == function_return_addr)
                break;
            ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
            wait(&status);
            ptrace(PTRACE_GETREGS, pid, 0, &regs);
            if (regs.rax != 0) {
                printf("PRF:: syscall in %lld returned with %lld\n", regs.rip - 2, regs.rax);
            }
        }

        //remove the breakpoint of return address
        ptrace(PTRACE_POKETEXT, pid, (void *) function_return_addr, (void *) function_finish);
        regs.rip -= 1;
        ptrace(PTRACE_SETREGS, pid, 0, &regs);
    }
}

pid_t run_target(char** argv)
{
    pid_t pid = fork();
    if (pid > 0) {
        return pid;

    } else if (pid == 0) {
        /* Allow tracing of this process */
        if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
            perror("ptrace");
            exit(1);
        }
        /* Replace this process's image with the given program */
        execv(argv[2], argv + 2);
        perror("execv failed");
    } else {
        // fork error
        perror("fork");
        exit(1);
    }
    return pid;
}

int main(int argc, char** argv)
{
    Elf64_Addr function_addr = get_function_addr(argv[1], argv[2]);
    if (function_addr == -1){
        return -1;
    }
    pid_t pid = run_target(argv);
    run_track_syscalls_in_function(pid, function_addr);
    return 0;
}

