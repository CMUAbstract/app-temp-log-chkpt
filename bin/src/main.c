#include <msp430.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <libmsp/mem.h>
#include <libio/log.h>
#include <msp-math.h>
#include <msp-builtins.h>
#include <wisp-base.h>

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

#define TASK_MAIN                   0
#define TASK_INIT_DICT              1
#define TASK_COMPRESS               2
#define TASK_SAMPLE                 3
#define TASK_FIND_CHILD             4
#define TASK_FIND_SIBLING           5
#define TASK_ADD_NODE               6
#define TASK_ADD_NODE_INIT          7
#define TASK_ADD_NODE_FIND_LAST     8
#define TASK_ADD_NODE_LINK_SIBLING  9
#define TASK_ADD_NODE_LINK_CHILD   10
#define TASK_APPEND_COMPRESSED     11
#define TASK_PRINT                 12

#ifdef DINO

#define TASK_BOUNDARY(t) \
        DINO_TASK_BOUNDARY_MANUAL(NULL); \
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

#define TASK_BOUNDARY(t) SET_CURTASK(t)

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

static sample_t acquire_sample(letter_t prev_sample)
{
    TASK_BOUNDARY(TASK_SAMPLE);
    DINO_RESTORE_NONE();

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
        DINO_VERSION_VAL(unsigned, dict->node_count, dict_node_count);
        TASK_BOUNDARY(TASK_INIT_DICT);
        DINO_RESTORE_VAL(dict->node_count, dict_node_count);

        curtask = l; // HACK for progress display

        node_t *node = &dict->nodes[l];
        node->letter = l;
        node->sibling = 0;
        node->child = 0;

        dict->node_count++;
        LOG("init dict: node count %u\r\n", dict->node_count);
    }
}

index_t find_child(letter_t letter, index_t parent, dict_t *dict)
{
    TASK_BOUNDARY(TASK_FIND_CHILD);
    DINO_RESTORE_NONE();

    node_t *parent_node = &dict->nodes[parent];

    LOG("find child: l %u p %u c %u\r\n", letter, parent, parent_node->child);

    if (parent_node->child == NIL) {
        LOG("find child: not found (no children)\r\n");
        return NIL;
    }

    index_t sibling = parent_node->child;
    while (sibling != NIL) {

        TASK_BOUNDARY(TASK_FIND_SIBLING);
        DINO_RESTORE_NONE();

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
    TASK_BOUNDARY(TASK_ADD_NODE);
    DINO_RESTORE_NONE();

    if (dict->node_count == DICT_SIZE) {
        PRINTF("add node: table full\r\n");
        while(1); // bail for now
    }

    DINO_VERSION_VAL(unsigned, dict->node_count, dict_node_count);
    TASK_BOUNDARY(TASK_ADD_NODE_INIT);
    DINO_RESTORE_VAL(dict->node_count, dict_node_count);

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

            TASK_BOUNDARY(TASK_ADD_NODE_FIND_LAST);
            DINO_RESTORE_NONE();

            LOG("add node: sibling %u, l %u s %u\r\n",
                sibling, letter, sibling_node->sibling);
            sibling = sibling_node->sibling;
            sibling_node = &dict->nodes[sibling];
        }

        TASK_BOUNDARY(TASK_ADD_NODE_LINK_SIBLING);
        DINO_RESTORE_NONE();

        // Link-in the new node
        LOG("add node: last sibling %u\r\n", sibling);
        dict->nodes[sibling].sibling = node_index;
    } else {
        TASK_BOUNDARY(TASK_ADD_NODE_LINK_CHILD);
        DINO_RESTORE_NONE();

        LOG("add node: is only child\r\n");
        dict->nodes[parent].child = node_index;
    }
}

void append_compressed(index_t parent, log_t *log)
{
    DINO_VERSION_VAL(unsigned, log->count, log_count);
    TASK_BOUNDARY(TASK_APPEND_COMPRESSED);
    DINO_RESTORE_VAL(log->count, log_count);

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

    TASK_BOUNDARY(TASK_MAIN);
    DINO_RESTORE_NONE();

    init_dict(&dict);

    // Initialize the pointer into the dictionary to one of the root nodes
    // Assume all streams start with a fixed prefix ('0'), to avoid having
    // to letterize this out-of-band sample.
    letter_t letter = 0;

    unsigned letter_idx = 0;
    index_t parent, child;
    sample_t sample, prev_sample = 0;

    log.sample_count = 1; // count the initial sample (see above)
    log.count = 0; // init compressed counter

    while (1) {
        TASK_BOUNDARY(TASK_COMPRESS);
        DINO_RESTORE_NONE();

        child = (index_t)letter; // relyes on initialization of dict
        LOG("compress: parent %u\r\n", child); // naming is odd due to loop

        do {

            DINO_VERSION_VAL(unsigned, log.sample_count, log_sample_count);
            TASK_BOUNDARY(TASK_MAIN);
            DINO_RESTORE_VAL(log.sample_count, log_sample_count);

            if (letter_idx == 0) {
                sample = acquire_sample(prev_sample);
                prev_sample = sample;
            }
            unsigned letter_shift = LETTER_SIZE_BITS * letter_idx;
            letter = (sample & (LETTER_MASK << letter_shift)) >> letter_shift;
            LOG("letterize: sample %x letter %x (%u)\r\n",
                sample, letter, letter);
            letter_idx++;
            if (letter_idx == NUM_LETTERS_IN_SAMPLE)
                letter_idx = 0;

            log.sample_count++;
            parent = child;
            child = find_child(letter, parent, &dict);
        } while (child != NIL);

        append_compressed(parent, &log);
        add_node(letter, parent, &dict);

        if (log.count == BLOCK_SIZE) {

            TASK_BOUNDARY(TASK_PRINT);
            DINO_RESTORE_NONE();

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
