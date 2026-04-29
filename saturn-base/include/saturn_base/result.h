/*
 * libs/saturn-base — shared result type and error codes.
 *
 * Every lib in the saturn lobby project that returns a status indicator
 * uses saturn_result_t. Zero is success; negative values are errors from
 * the enum below. Functions that compute a quantity return that quantity
 * directly with a documented sentinel only if the operation can fail in a
 * way uint cannot represent.
 *
 * Header-only. No implementation file.
 */

#ifndef SATURN_BASE_RESULT_H
#define SATURN_BASE_RESULT_H

typedef int saturn_result_t;

enum {
    SATURN_OK              =    0,
    SATURN_ERR_INVALID     =   -1, /* bad argument, malformed input */
    SATURN_ERR_NOT_FOUND   =   -2, /* lookup miss (key, slot, peripheral) */
    SATURN_ERR_NO_SPACE    =   -3, /* backup full, command-table cap, etc. */
    SATURN_ERR_BUSY        =   -4, /* peripheral busy, send would block */
    SATURN_ERR_TIMEOUT     =   -5,
    SATURN_ERR_DISCONNECT  =   -6, /* connection / cable lost */
    SATURN_ERR_VERSION     =   -7, /* schema or protocol mismatch */
    SATURN_ERR_HARDWARE    =   -8, /* SMPC reports something we don't know */
    SATURN_ERR_NOT_READY   =   -9, /* lib not initialised, PAL not installed */
    SATURN_ERR_INTERNAL    = -100  /* assertion-grade bug — should not occur */
};

#endif /* SATURN_BASE_RESULT_H */
