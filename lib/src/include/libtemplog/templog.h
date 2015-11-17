#ifndef TEMPLOG_H
#define TEMPLOG_H

#define DICT_SIZE        128 // TODO: support small table by re-initing when full
#define BLOCK_SIZE        64
#define NUM_LETTERS     (0x0F + 1)

typedef unsigned index_t;
typedef unsigned letter_t;

// NOTE: can't use pointers, since need to ChSync, etc
typedef struct _node_t {
    letter_t letter; // 'letter' of the alphabet
    index_t sibling; // this node is a member of the parent's children list
    index_t child;   // link-list of children
} node_t;

typedef struct _dict_t {
    node_t nodes[DICT_SIZE];
    unsigned node_count;
} dict_t;

typedef struct _log_t {
    index_t data[BLOCK_SIZE];
    unsigned count;
    unsigned sample_count;
} log_t;

#endif // TEMPLOG_H

