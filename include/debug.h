#ifndef _DEBUG_H
#define _DEBUG_H
#ifdef DEBUG
#define dprintf(fmt, args...)					\
	do {							\
		fprintf(stderr, "%s:%d: " fmt,			\
			__FILE__, __LINE__, ##args);		\
	} while (0)
#else /* !defined(DEBUG) */
#define dprintf(fmt, args...)					\
	do {							\
	} while (0)
#endif /* !defined(DEBUG) */
#endif /* _DEBUG_H */
