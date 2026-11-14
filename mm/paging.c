/*
 * mm/paging.c — Paginazione a due livelli di x86.
 *
 * L'IDEA: una volta accesa la paginazione, OGNI indirizzo usato dal codice è
 * "virtuale". Prima di toccare la RAM, la CPU (con un suo circuito, la MMU) lo
 * traduce in un indirizzo fisico consultando delle tabelle che scriviamo noi.
 * Chi controlla le tabelle decide cosa ogni programma può vedere: è la base
 * dell'isolamento tra processi.
 *
 * COME È FATTO UN INDIRIZZO VIRTUALE (32 bit):
 *
 *      31         22 21         12 11          0
 *     +-------------+-------------+-------------+
 *     | indice nella| indice nella| spostamento |
 *     | directory   | tabella     | nella pagina|
 *     |  (10 bit)   |  (10 bit)   |  (12 bit)   |
 *     +-------------+-------------+-------------+
 *
 * LA TRADUZIONE, in due passi:
 *
 *     registro CR3 --> PAGE DIRECTORY (1024 righe)
 *                        riga [indice directory] --> PAGE TABLE (1024 righe)
 *                                                     riga [indice tabella] --> pagina fisica
 *                                                                               + spostamento
 *
 * Ogni riga è un numero a 32 bit: nei 20 bit alti l'indirizzo fisico (sempre
 * multiplo di 4096, quindi i 12 bit bassi sarebbero zeri sprecati), nei 12 bit
 * bassi i permessi (presente, scrivibile, utente...).
 * Una page table copre 1024 x 4 KiB = 4 MiB; la directory 1024 x 4 MiB = 4 GiB.
 *
 * COLLEGAMENTO CON LA TEORIA: è la "paginazione gerarchica a due livelli". Il
 * vantaggio rispetto a un'unica tabella: le page table delle zone non usate
 * semplicemente non esistono (riga della directory "non presente").
 * La cache delle traduzioni vista a lezione, il TLB, compare in paging_map_page().
 *
 * LA NOSTRA DISPOSIZIONE DELLA MEMORIA VIRTUALE:
 *   - tutta la RAM è mappata "in identità": indirizzo virtuale = indirizzo fisico.
 *     Così il kernel continua a funzionare identico dopo l'accensione, e può
 *     scrivere in qualsiasi pagina fisica usandone direttamente l'indirizzo;
 *   - la pagina 0 NON è mappata: usare un puntatore nullo provoca un page fault
 *     (l'equivalente della NullPointerException) invece di leggere dati a caso;
 *   - le altre zone (es. l'heap del kernel, mm/kheap.c) si mappano a richiesta.
 */

#include <stdint.h>
#include "mm/paging.h"
#include "mm/pmm.h"
#include "kernel/interrupts.h"
#include "lib/kprintf.h"
#include "lib/string.h"

#define ENTRIES_PER_TABLE 1024

/* Maschera per separare le due parti di una riga:
 * 0xFFFFF000 ha a 1 i 20 bit alti (l'indirizzo) e a 0 i 12 bassi (i permessi). */
#define ADDRESS_MASK 0xFFFFF000

/*
 * La page directory del kernel: un array di 1024 numeri a 32 bit che occupa
 * esattamente una pagina. Non è un array dichiarato qui: è una pagina chiesta
 * all'allocatore fisico, e questo puntatore ne contiene l'indirizzo.
 * Grazie alla mappatura in identità, quell'indirizzo fisico vale anche come virtuale.
 */
static uint32_t *page_directory = 0;

/* Estraggono i due indici da un indirizzo virtuale.
 * `>> 22` butta via i 22 bit bassi e lascia i 10 più alti.
 * `>> 12` butta via lo spostamento; `& 0x3FF` tiene solo 10 bit (0x3FF = 1023). */
static uint32_t directory_index(uint32_t virtual_address)
{
    return virtual_address >> 22;
}

static uint32_t table_index(uint32_t virtual_address)
{
    return (virtual_address >> 12) & 0x3FF;
}

/*
 * Restituisce la page table che copre `virtual_address`.
 * Se non esiste ancora: con `create` = 1 la crea, con `create` = 0 restituisce 0.
 */
static uint32_t *get_page_table(uint32_t virtual_address, int create)
{
    uint32_t index = directory_index(virtual_address);

    if (!(page_directory[index] & PAGE_PRESENT)) {
        if (!create) {
            return 0;
        }

        /* Una page table è grande esattamente una pagina: la chiediamo al PMM. */
        uint32_t table_address = pmm_alloc_page();
        if (table_address == 0) {
            return 0;
        }

        /* Il PMM non azzera le pagine: senza questo memset la tabella conterrebbe
         * dati vecchi, cioè mappature casuali. Tutto a zero = "nessuna pagina presente". */
        memset((void *) table_address, 0, PAGE_SIZE);

        /* Nella riga della directory teniamo i permessi larghi: quelli veri si
         * decidono pagina per pagina, nella page table. */
        page_directory[index] = table_address | PAGE_PRESENT | PAGE_WRITABLE;
    }

    /* Togliamo i bit dei permessi: resta l'indirizzo della tabella, che il cast
     * trasforma in un puntatore per poterla usare come array. */
    return (uint32_t *) (page_directory[index] & ADDRESS_MASK);
}

int paging_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags)
{
    uint32_t *table = get_page_table(virtual_address, 1);
    if (table == 0) {
        return 0;
    }

    /* Una pagina utente deve essere raggiungibile dall'utente a OGNI livello:
     * il permesso va quindi acceso anche nella riga della directory. */
    if (flags & PAGE_USER) {
        page_directory[directory_index(virtual_address)] |= PAGE_USER;
    }

    /* La riga: indirizzo fisico nei bit alti, permessi nei bit bassi. */
    table[table_index(virtual_address)] =
        (physical_address & ADDRESS_MASK) | (flags & 0xFFF) | PAGE_PRESENT;

    /*
     * TLB (Translation Lookaside Buffer): la CPU ricorda le traduzioni recenti per
     * non rileggere le tabelle a ogni accesso. Se cambiamo una riga dobbiamo dirle
     * di dimenticare la vecchia traduzione di quella pagina: lo fa `invlpg`.
     * Dimenticarlo dà bug subdoli: la CPU continua a usare la mappatura vecchia.
     */
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");

    return 1;
}

void paging_unmap_page(uint32_t virtual_address)
{
    uint32_t *table = get_page_table(virtual_address, 0);
    if (table == 0) {
        return;     /* non c'è nemmeno la tabella: niente da cancellare */
    }

    table[table_index(virtual_address)] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

uint32_t paging_get_physical(uint32_t virtual_address)
{
    /* Rifacciamo in C, passo per passo, ciò che la MMU fa in hardware. */
    uint32_t *table = get_page_table(virtual_address, 0);
    if (table == 0) {
        return 0;
    }

    uint32_t entry = table[table_index(virtual_address)];
    if (!(entry & PAGE_PRESENT)) {
        return 0;
    }

    /* Indirizzo della pagina fisica + spostamento dentro la pagina (i 12 bit bassi). */
    return (entry & ADDRESS_MASK) + (virtual_address & 0xFFF);
}

/*
 * Gestore dell'eccezione 14, il PAGE FAULT: la CPU la genera quando un indirizzo
 * virtuale non si può tradurre, o quando l'accesso viola i permessi.
 *
 * COLLEGAMENTO CON LA TEORIA: nei sistemi veri il page fault non è sempre un errore.
 * È il meccanismo con cui si realizzano la paginazione su richiesta (demand paging),
 * lo swap su disco e il copy-on-write: il kernel sistema le tabelle e fa ripetere
 * l'istruzione. Noi per ora non abbiamo nulla da sistemare: è sempre un bug.
 */
static void page_fault_handler(struct registers *regs)
{
    /* La CPU mette nel registro CR2 l'indirizzo che ha causato il fault.
     * I registri di controllo si leggono solo con un'istruzione `mov` apposita. */
    uint32_t fault_address;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));

    /* Il codice di errore spiega cosa è successo, un bit per informazione. */
    int present = regs->err_code & 0x1;     /* 0 = pagina non mappata, 1 = permessi violati */
    int write   = regs->err_code & 0x2;     /* 0 = lettura, 1 = scrittura                   */
    int user    = regs->err_code & 0x4;     /* 0 = stava girando il kernel, 1 = un programma utente */

    kprintf("\nPAGE FAULT all'indirizzo 0x%x\n", fault_address);
    kprintf("  %s in %s, %s\n",
            write   ? "scrittura" : "lettura",
            user    ? "modalita' utente" : "modalita' kernel",
            present ? "permessi insufficienti" : "pagina non mappata");

    if (fault_address < PAGE_SIZE) {
        kprintf("  (indirizzo nella pagina 0: quasi certamente un puntatore nullo)\n");
    }

    interrupt_panic(regs);
}

void paging_init(void)
{
    /* La directory: una pagina, azzerata (nessuna page table presente). */
    page_directory = (uint32_t *) pmm_alloc_page();
    memset(page_directory, 0, PAGE_SIZE);

    /*
     * Mappatura in identità di tutta la RAM: virtuale = fisico.
     * Si parte da PAGE_SIZE e non da 0: la pagina 0 resta volutamente non mappata.
     * Le page table necessarie vengono create da paging_map_page() man mano
     * (una ogni 4 MiB: per 128 MiB di RAM sono 32 pagine).
     *
     * In questo momento la paginazione è ancora spenta, quindi stiamo scrivendo
     * nelle tabelle usando indirizzi fisici: nessun problema dell'uovo e della gallina.
     */
    uint32_t memory_top = pmm_get_memory_top();
    for (uint32_t address = PAGE_SIZE; address < memory_top; address += PAGE_SIZE) {
        if (!paging_map_page(address, address, PAGE_WRITABLE)) {
            kprintf("Paginazione: ERRORE, memoria esaurita durante la mappatura\n");
            return;
        }
    }

    /* Il gestore va registrato PRIMA di accendere: da lì in poi un errore nelle
     * tabelle si manifesta come page fault, e vogliamo vederlo. */
    interrupt_register_handler(INT_PAGE_FAULT, page_fault_handler);

    /* CR3 = indirizzo FISICO della page directory: è da lì che la MMU parte. */
    __asm__ volatile ("mov %0, %%cr3" : : "r"(page_directory));

    /*
     * Accensione: bit 31 (PG) del registro CR0.
     * Leggiamo CR0, accendiamo quel solo bit con un OR, riscriviamo.
     * L'istruzione successiva viene già eseguita con gli indirizzi tradotti: se il
     * codice che stiamo eseguendo non fosse mappato in identità, la CPU non
     * troverebbe più la prossima istruzione e si riavvierebbe (triple fault).
     */
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));

    kprintf("Paginazione accesa: RAM mappata in identita' fino a 0x%x, pagina 0 esclusa\n",
            memory_top);
}
