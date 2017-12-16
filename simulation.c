#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <time.h>

#include "config.h"


/*
 * A cache simulation:
 *
 * Array (2^n) of lines that contain blocks (2^n) - offsets
 * Blocks that contain multiple cached values (tag, value) - indexes
 *
 * Prefetcher (on new read or write)
 * Cache policy (replacing value when no place to add tag)
 * Associativity
 *
 * Example:
 *
 * Mem 1024B
 * Block 128B
 * Assoc 4
 *
 * Indexes:
 * That gives 2 indexes = Mem/(block*assoc), nb bits (log(2)/log(2) = 1 bit
 * [ 4 first blocks index 1][ 4 others blocks index 2]
 *
 * Offset:
 * block 128B -> log(128)/log(2) = 7 bits
 * 0x123456789ABCDEF
 * 0b1001000110100010101100111100010011010101111001101 1 1101111
 * offset = 1101111
 * index = 1
 * tag = 1001000110100010101100111100010011010101111001101
 * whatever the offset if the index is the same and tag the same then
 * it's the same address block
 *
 * Tag can be compared with XNOR
 *
 * References:
 * https://people.freebsd.org/~lstewart/articles/cpumemory.pdf
 * https://en.wikipedia.org/wiki/Cache_(computing)
 * http://csillustrated.berkeley.edu/PDFs/handouts/cache-3-associativity-handout.pdf
 * https://www.cs.umd.edu/class/sum2003/cmsc311/Notes/Memory/fully.html
 * https://stackoverflow.com/questions/30097648/difference-between-a-direct-mapped-cache-and-fully-associative-cache
 * https://github.com/auxiliary/CacheSimulator
 */

struct line {
	unsigned long index;
	unsigned long offset;
	unsigned long tag;
	time_t last_access;
	char buff[BLOCKS_SIZE];
};

typedef struct line* blocks;
typedef blocks* cache;

struct action {
	char rw;
	unsigned long address;
};

void
help(char *argv)
{
	printf("usage: %s <mem_file>\n", argv);
}

/*
 * Read a text file with some addresses and actions:
 *
 * R: 0x<address>
 * W: 0x<address>
 */
struct action*
read_action_from_file(char *file_name)
{
	FILE *f;
	char rw;
	unsigned long address;
	struct action *actions;
	unsigned int nb_actions;
	unsigned int current_action;

	f = fopen(file_name, "r");
	if (f == NULL) {
		perror(NULL);
		exit(1);
	}

	nb_actions = 40;
	actions = malloc(sizeof(struct action) * nb_actions);
	current_action = 0;
	while (fscanf(f, " %c: 0x%12x", &rw, &address) != EOF) {
		if (current_action + 2 > nb_actions) {
			nb_actions += 40;
			actions = realloc(actions, sizeof(struct action) * nb_actions);
		}
		actions[current_action].rw = rw;
		actions[current_action].address = address;
		current_action++;
	}

	// use this as the delimiter
	actions[current_action].rw = '\0';

	fclose(f);
	return actions;
}

struct line
line_from_address(unsigned long address)
{
	struct line l;

	l.offset = address & ((1 << BLOCKS_SIZE_N) - 1);
	l.index = (address >> BLOCKS_SIZE_N) & ((1 << NB_BLOCKS_N) - 1);
	l.tag = (address >> (BLOCKS_SIZE_N+NB_BLOCKS_N));
	l.last_access = time(NULL);
	//printf("line info: offset: %4x, index: %4x, tag: %10x\n",
	//	l.offset, l.index, l.tag);
	return l;
}

char
in_cache(cache c, struct line l)
{
	unsigned int i;

	for (i = 0; i < ASSOCIATIVITY; i++) {
		// could be xnor
		if (c[l.index][i].tag == l.tag) {
			return 1;
		}
	}

	return 0;
}

void
add_to_cache(cache c, struct line l)
{
	unsigned int i, oldest;
	time_t oldest_time;

	oldest_time = time(NULL);
	for (i = 0; i < ASSOCIATIVITY; i++) {
		// found an empty line then use it
		if (c[l.index][i].tag == 0x00) {
			c[l.index][i] = l;
			return;
		} else {
			if (c[l.index][i].last_access < oldest_time) {
				oldest_time = c[l.index][i].last_access;
				oldest = i;
			}
		}
	}
	// there's no space left in the group
	// what do we do?
	// swap the oldest one by this one
	c[l.index][oldest] = l;
}

void
execute_mem(struct action *actions)
{
	cache c;
	unsigned int i;
	struct line l;
	unsigned int hits;
	unsigned int misses;

	hits = misses = 0;

	c = malloc(sizeof(blocks) * NB_BLOCKS);
	if (c == NULL) {
		perror(NULL);
		exit(1);
	}

	for (i = 0; i < NB_BLOCKS; i++) {
		c[i] = calloc(ASSOCIATIVITY, sizeof(struct line));
		if (c[i] == NULL) {
			perror(NULL);
			exit(1);
		}
	}

	for (i = 0; actions[i].rw != '\0'; i++) {
		l = line_from_address(actions[i].address);
		if (in_cache(c, l)) {
			hits++;
		} else {
			add_to_cache(c,l);
			if (PREFETCHING) {
				l = line_from_address(actions[i].address+BLOCKS_SIZE);
				if (!in_cache(c, l)) {
					add_to_cache(c, l);
					misses++;
				}
			}
			misses++;
		}
	}

	printf("Hits: %d, Misses: %d\n", hits, misses);

	// cleanup
	for (i = 0; i < NB_BLOCKS; i++) {
		free(c[i]);
	}
	free(c);
}

int
main(int argc, char **argv)
{
	struct action *actions;

	if (argc < 2) {
		help(argv[0]);
		exit(1);
	}

	actions = read_action_from_file(argv[1]);
	execute_mem(actions);

	if (actions != NULL) {
		free(actions);
	}

	return 0;
}

