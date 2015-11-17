#include <msp430.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <libmsp/mem.h>
#include <libio/log.h>
#include <msp-math.h>
#include <msp-builtins.h>

#ifdef CONFIG_EDB
#include <libedb/edb.h>
#else
#define ENERGY_GUARD_BEGIN()
#define ENERGY_GUARD_END()
#endif

#ifdef DINO
#include <dino.h>
#endif

#include "libtemplog/templog.h"
#include "libtemplog/print.h"

#include "pins.h"

// #define SHOW_PROGRESS_ON_LED

/* This is for progress reporting only */
#define SET_CURTASK(t) curtask = t

#define TASK_MAIN                   1

#ifdef DINO

#define TASK_BOUNDARY(t, x) \
        DINO_TASK_BOUNDARY_MANUAL(x); \
        SET_CURTASK(t); \

#define DINO_RESTORE_NONE() \
        DINO_REVERT_BEGIN() \
        DINO_REVERT_END() \

#define DINO_RESTORE_PTR(nm, type) \
        DINO_REVERT_BEGIN() \
        DINO_REVERT_PTR(type, nm); \
        DINO_REVERT_END() \

#define DINO_RESTORE_VAL(nm, label) \
        DINO_REVERT_BEGIN() \
        DINO_REVERT_VAL(nm, label); \
        DINO_REVERT_END() \

#else // !DINO

#define TASK_BOUNDARY(t, x) SET_CURTASK(t)

#define DINO_RESTORE_CHECK()
#define DINO_VERSION_PTR(...)
#define DINO_VERSION_VAL(...)
#define DINO_RESTORE_NONE()
#define DINO_RESTORE_PTR(...)
#define DINO_RESTORE_VAL(...)
#define DINO_REVERT_BEGIN(...)
#define DINO_REVERT_END(...)
#define DINO_REVERT_VAL(...)

#endif // !DINO

#define NIL 0 // like NULL, but for indexes, not real pointers

static __nv unsigned curtask;

static letter_t acquire_sample(letter_t prev_sample)
{
#ifdef TEST_SAMPLE_DATA
    //letter_t sample = rand() & 0x0F;
    letter_t sample = (prev_sample + 1) & 0x03;
    return sample;
#else
    ADC12CTL0 &= ~ADC12ENC; // disable conversion so we can set control bits
    ADC12CTL0 = ADC12SHT0_2 + ADC12ON; // sampling time, ADC12 on
    ADC12CTL1 = ADC12SHP + ADC12CONSEQ_0; // use sampling timer, single-channel, single-conversion

    ADC12CTL3 &= ADC12TCMAP; // enable temperature sensor
    ADC12MCTL0 = ADC12INCH_30; // temp sensor

    ADC12CTL0 |= ADC12ENC; // enable ADC

    // Trigger
    ADC12CTL0 &= ~ADC12SC;  // 'start conversion' bit must be toggled
    ADC12CTL0 |= ADC12SC; // start conversion

    while (ADC12CTL1 & ADC12BUSY); // wait for conversion to complete

    ADC12CTL3 &= ~ADC12TCMAP; // disable temperature sensor

    sample_t sample = ADC12MEM0;
    LOG("sample: %04x\r\n", sample);

    //volatile uint32_t delay = 0xfff;
    //while (delay--);

    return sample;
#endif
}

void init_dict(dict_t *dict)
{
    letter_t l;

    LOG("init dict\r\n");

    for (l = 0; l < NUM_LETTERS; ++l) {
        node_t *node = &dict->nodes[l];
        node->letter = l;
        node->sibling = 0;
        node->child = 0;

        dict->node_count++;
    }
}

index_t find_child(letter_t letter, index_t parent, dict_t *dict)
{
    node_t *parent_node = &dict->nodes[parent];

    LOG("find child: l %u p %u c %u\r\n", letter, parent, parent_node->child);

    if (parent_node->child == NIL) {
        LOG("find child: not found (no children)\r\n");
        return NIL;
    }

    index_t sibling = parent_node->child;
    while (sibling != NIL) {

        node_t *sibling_node = &dict->nodes[sibling];

        LOG("find child: l %u, s %u l %u s %u\r\n", letter,
            sibling, sibling_node->letter, sibling_node->sibling);

        if (sibling_node->letter == letter) { // found
            LOG("find child: found %u\r\n", sibling);
            return sibling;
        } else {
            sibling = sibling_node->sibling;
        }
    }

    LOG("find child: not found (no match)\r\n");
    return NIL; 
}

void add_node(letter_t letter, index_t parent, dict_t *dict)
{
    if (dict->node_count == DICT_SIZE) {
        PRINTF("add node: table full\r\n");
        while(1); // bail for now
    }

    // Initialize the new node
    node_t *node = &dict->nodes[dict->node_count];

    node->letter = letter;
    node->sibling = NIL;
    node->child = NIL;

    index_t node_index = dict->node_count++;

    index_t child = dict->nodes[parent].child;

    LOG("add node: i %u l %u, p: %u pc %u\r\n",
        node_index, letter, parent, child);

    if (child) {
        LOG("add node: is sibling\r\n");

        // Find the last sibling in list
        index_t sibling = child;
        node_t *sibling_node = &dict->nodes[sibling];
        while (sibling_node->sibling != NIL) {
            LOG("add node: sibling %u, l %u s %u\r\n",
                sibling, letter, sibling_node->sibling);
            sibling = sibling_node->sibling;
            sibling_node = &dict->nodes[sibling];
        }

        // Link-in the new node
        LOG("add node: last sibling %u\r\n", sibling);
        dict->nodes[sibling].sibling = node_index;
    } else {
        LOG("add node: is only child\r\n");
        dict->nodes[parent].child = node_index;
    }
}

void append_compressed(index_t parent, log_t *log)
{
    LOG("append comp: p %u cnt %u\r\n", parent, log->count);
    log->data[log->count++] = parent;
}

void init()
{
    WISP_init();

#ifdef CONFIG_EDB
    debug_setup();
#endif

    INIT_CONSOLE();

    __enable_interrupt();

    GPIO(PORT_LED_1, DIR) |= BIT(PIN_LED_1);
    GPIO(PORT_LED_2, DIR) |= BIT(PIN_LED_2);
#if defined(PORT_LED_3)
    GPIO(PORT_LED_3, DIR) |= BIT(PIN_LED_3);
#endif

#if defined(PORT_LED_3) // when available, this LED indicates power-on
    GPIO(PORT_LED_3, OUT) |= BIT(PIN_LED_3);
#endif

#ifdef SHOW_PROGRESS_ON_LED
    blink(1, SEC_TO_CYCLES * 5, LED1 | LED2);
#endif

    EIF_PRINTF(".%u.\r\n", curtask);
}

int main()
{
    // Mementos can't handle globals: it restores them to .data, when they are
    // in .bss... So, for now, just keep all data on stack.
#if defined(MEMENTOS) && !defined(MEMENTOS_NONVOLATILE)
    dict_t dict;
    log_t log;
#else // !MEMENTOS || MEMENTOS_NONVOLATILE
    static __nv dict_t dict;
    static __nv log_t log;
#endif // !MEMENTOS || MEMENTOS_NONVOLATILE

#ifndef MEMENTOS
    init();
#endif

    DINO_RESTORE_CHECK();

    init_dict(&dict);

    // Get the first sample (letter) to start at a root node of the dict
    letter_t prev_sample = 0;
    letter_t sample = acquire_sample(prev_sample);
    prev_sample = sample;

    index_t parent, child;

    log.sample_count = 1; // count the initial sample (see above)
    log.count = 0; // init compressed counter

    while (1) {
        TASK_BOUNDARY(TASK_MAIN, NULL);
        DINO_RESTORE_NONE();

        child = (index_t)sample; // relyes on initialization of dict
        LOG("compress: parent %u\r\n", child); // naming is odd due to loop

        do {
            sample = acquire_sample(prev_sample);
            prev_sample = sample;
            log.sample_count++;
            parent = child;
            child = find_child(sample, parent, &dict);
        } while (child != NIL);

        append_compressed(parent, &log);
        add_node(sample, parent, &dict);

        if (log.count == BLOCK_SIZE) {
            print_log(&log);
            log.count = 0;
            log.sample_count = 0;

            // For now do only one block
            while(1);
        }

#ifdef CONT_POWER
        volatile uint32_t delay = 0x4ffff;
        while (delay--);
#endif
    }

    return 0;
}
