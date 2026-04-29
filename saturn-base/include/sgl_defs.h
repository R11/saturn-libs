/**
 * sgl_defs.h - SGL type definitions and function declarations
 *
 * Minimal SGL declarations for bare-metal Saturn programming.
 * Provides types and functions needed by the Saturn PAL without
 * requiring the full SGL headers (which have parsing issues with modern GCC).
 */

#ifndef SGL_DEFS_H
#define SGL_DEFS_H

#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Basic SGL Types
 *============================================================================*/

typedef uint8_t     Uint8;
typedef int8_t      Sint8;
typedef uint16_t    Uint16;
typedef int16_t     Sint16;
typedef uint32_t    Uint32;
typedef int32_t     Sint32;

/* SGL fixed-point type (16.16 format) */
typedef Sint32 FIXED;
#define toFIXED(x) ((FIXED)(x) << 16)

/* Boolean type used by SGL functions */
typedef int Bool;

/*============================================================================
 * SGL 3D Geometry Types
 *============================================================================*/

typedef FIXED  POINT[3];     /* 3D vertex: X, Y, Z as FIXED */
typedef FIXED  VECTOR[3];    /* 3D direction vector */
typedef Sint16 ANGLE;        /* 0-65535 maps to 0-360 degrees */
typedef FIXED  MATRIX[4][3]; /* 4x3 transformation matrix */

/* Polygon face: normal + 4 vertex indices */
typedef struct {
    VECTOR  norm;          /* Face normal (FIXED x3) */
    Uint16  Vertices[4];   /* Vertex indices (0-based) */
} POLYGON;

/* Polygon attributes: rendering flags, color, texture info */
typedef struct {
    Uint8   flag;   /* Display flags (DUAL_PLANE, etc.) */
    Uint8   sort;   /* Sort mode (SORT_MIN, etc.) */
    Uint16  texno;  /* Texture number */
    Uint16  atrb;   /* Attribute bits (color mode, gouraud, etc.) */
    Uint16  colno;  /* Color number or RGB555 value */
    Uint16  gstb;   /* Gouraud shading table */
    Uint16  dir;    /* Direction/flip */
} ATTR;

/* Polygon data: vertex table + polygon table + attribute table */
typedef struct {
    POINT   *pntbl;      /* Pointer to vertex array */
    Uint32   nbPoint;    /* Number of vertices */
    POLYGON *pltbl;      /* Pointer to polygon array */
    Uint32   nbPolygon;  /* Number of polygons */
    ATTR    *attbl;      /* Pointer to attribute array */
} PDATA;

/* ATTR constants */
#define CL32KRGB     0x0800  /* 32K direct RGB color mode */
#define SPenb        0x0040  /* Sprite enable */
#define No_Window    0x0000  /* No window clipping */
#define No_Gouraud   0x0020  /* No gouraud shading (flat) */
#define No_Texture   0x0000  /* No texture mapping */
#define SORT_MIN     0x00    /* Sort by minimum Z */
#define SORT_CEN     0x01    /* Sort by center Z */
#define SORT_MAX     0x02    /* Sort by maximum Z */
#define DUAL_PLANE   0x00    /* Single-sided polygon */
#define FUNC_Flat    0x00    /* Flat shading function */

/* ---- saturn-base extras (added by libs/) ----
 * The fields below are SGL constants/macros used by libs/saturn-vdp1
 * and libs/saturn-vdp2 that aren't in saturn-tools' minimal subset. Kept
 * here rather than in a sibling header so every saturn/ shell that
 * already includes "sgl_defs.h" gets them transparently. */

/* ATTR.flag values. SGL's SL_DEF.H defines these as an enum:
 *   enum pln { Single_Plane, Dual_Plane };
 * so Single_Plane = 0 (back-face culling on) and Dual_Plane = 1
 * (render both sides). saturn-tools' minimal header had Single_Plane
 * as the only #define; we add Dual_Plane for 2D quads where vertex
 * winding can't be relied on. */
#ifndef Single_Plane
#define Single_Plane   0
#endif
#ifndef Dual_Plane
#define Dual_Plane     1
#endif

/* ATTR.atrb extra bits. SGL packs colour mode + mesh + window enables in
 * a single 16-bit field; only the ones we use are aliased here. */
#ifndef MESHoff
#define MESHoff        0x0000
#endif

/* ATTR.dir flip flags (active high). 0x00 = no flip. */
#ifndef sprNoflip
#define sprNoflip      0x0000
#endif

/* ATTR.atrb clip mode. */
#ifndef _ZmCL
#define _ZmCL          0x0000
#endif

/* gstb / option placeholders for the flat-quad case. */
#ifndef No_Option
#define No_Option      0x0000
#endif
#ifndef UseLight
#define UseLight       0x0000
#endif

/* RGB555 + opaque packer. r/g/b in 0..31. */
#ifndef C_RGB
#define C_RGB(r,g,b)   ((Uint16)(0x8000u | (((b) & 0x1F) << 10) | (((g) & 0x1F) << 5) | ((r) & 0x1F)))
#endif

/* Back-screen colour scan-line table address. SGL convention is to
 * place a 1-line table at the very end of VDP2 VRAM A1 — slBack1ColSet
 * writes one Uint16 (RGB555) at this offset. */
#ifndef BACK_CRAM
#define BACK_CRAM      ((void*)(VDP2_VRAM_A1 + 0x1FFFE))
#endif

/* scnNBG0..3 are defined further down in this file by the saturn-tools
 * vendored block. Don't re-define them here. */

/*============================================================================
 * SGL 3D Transform Functions
 *============================================================================*/

extern Bool slPushMatrix(void);
extern Bool slPopMatrix(void);
extern void slTranslate(FIXED x, FIXED y, FIXED z);
extern void slRotX(ANGLE angle);
extern void slRotY(ANGLE angle);
extern void slRotZ(ANGLE angle);
extern Bool slPutPolygon(PDATA *pdata);
extern void slPerspective(ANGLE fov);
extern void slLight(VECTOR light);
extern void slWindow(Sint16 left, Sint16 top, Sint16 right, Sint16 bottom, Sint16 z_limit, FIXED center_x, FIXED center_y);
extern void slLookAt(FIXED *camera, FIXED *target, ANGLE twist);

/*============================================================================
 * SGL Work Area Types (for workarea.c)
 *============================================================================*/

/* Work and Event buffer sizes */
#define WORK_SIZE   0x40    /* 64 bytes */
#define EVENT_SIZE  0x80    /* 128 bytes */

/* Work structure for SGL event/task system */
typedef struct work {
    struct work* next;
    Uint8 user[WORK_SIZE - sizeof(struct work*)];
} WORK;

/* Event structure for SGL event/task system */
typedef struct evnt {
    WORK*        work;
    struct evnt* next;
    struct evnt* before;
    void        (*exad)();
    Uint8        user[EVENT_SIZE - (sizeof(WORK*) + sizeof(struct evnt*) * 2 + sizeof(void (*)()))];
} EVENT;

/* Sound RAM base address */
#define SoundRAM  0x25a00000

/*============================================================================
 * TV Resolution Modes
 *============================================================================*/

enum tvsz {
    TV_320x224, TV_320x240, TV_320x256, TV_dummy1,
    TV_352x224, TV_352x240, TV_352x256, TV_dummy2,
    TV_640x224, TV_640x240, TV_640x256, TV_dummy3,
    TV_704x224, TV_704x240, TV_704x256, TV_dummy4,
    TV_320x448, TV_320x480, TV_320x512, TV_dummy5,
    TV_352x448, TV_352x480, TV_352x512, TV_dummy6,
    TV_640x448, TV_640x480, TV_640x512, TV_dummy7,
    TV_704x448, TV_704x480, TV_704x512, TV_dummy8
};

/*============================================================================
 * Texture Definition
 *============================================================================*/

typedef struct {
    Uint16 Hsize;
    Uint16 Vsize;
    Uint16 CGadr;
    Uint16 HVsize;
} TEXTURE;

/*============================================================================
 * Peripheral (Controller) Definitions
 *============================================================================*/

/* Button assignments for Saturn Pad (active-low: bit=0 means pressed) */
#define PER_DGT_KR  (1 << 15)   /* Direction key: right */
#define PER_DGT_KL  (1 << 14)   /* Direction key: left */
#define PER_DGT_KD  (1 << 13)   /* Direction key: down */
#define PER_DGT_KU  (1 << 12)   /* Direction key: up */
#define PER_DGT_ST  (1 << 11)   /* Start button */
#define PER_DGT_TA  (1 << 10)   /* Button A */
#define PER_DGT_TC  (1 << 9)    /* Button C */
#define PER_DGT_TB  (1 << 8)    /* Button B */
#define PER_DGT_TR  (1 << 7)    /* R trigger */
#define PER_DGT_TX  (1 << 6)    /* Button X */
#define PER_DGT_TY  (1 << 5)    /* Button Y */
#define PER_DGT_TZ  (1 << 4)    /* Button Z */
#define PER_DGT_TL  (1 << 3)    /* L trigger */

/* Peripheral data structure (from SGL SL_DEF.H:1404-1411) */
typedef struct {
    Uint8  id;           /* Peripheral ID */
    Uint8  ext;          /* Extension data size */
    Uint16 data;         /* Current button data (active-low) */
    Uint16 push;         /* Newly pressed buttons (edge detection) */
    Uint16 pull;         /* Newly released buttons (edge detection) */
    Uint32 dummy2[4];    /* Padding (matches SGL internal layout) */
} PerDigital;

/* Global peripheral data POINTER (provided by SGL) */
/* CRITICAL: This is a POINTER, not an array. SL_DEF.H:1499 declares:
 *   extern PerDigital* Smpc_Peripheral;
 * Using [] instead of * generates completely different machine code —
 * [] treats the symbol address AS the data, while * correctly follows
 * the pointer to the actual peripheral data. */
extern PerDigital* Smpc_Peripheral;

/* Connection status (provided by SGL) */
extern Uint8 Per_Connect1;

/*============================================================================
 * Core SGL Functions
 *============================================================================*/

/**
 * Initialize the SGL system
 * @param tv_mode - TV resolution (e.g., TV_320x224)
 * @param texture_table - Texture definition table (NULL for text-only)
 * @param framerate - Frame rate divisor (1 = 60fps, 2 = 30fps, etc.)
 */
extern void slInitSystem(Uint16 tv_mode, TEXTURE *texture_table, Sint8 framerate);

/**
 * Print text to screen
 * @param str - Null-terminated string
 * @param pos - Position from slLocate()
 */
extern void slPrint(char *str, void *pos);

/**
 * Get screen position for text
 * @param x - Column (0-39 for 320 width)
 * @param y - Row (0-27 for 224 height)
 * @return Position value for slPrint
 */
extern void *slLocate(Uint16 x, Uint16 y);

/**
 * Initialize V-BLANK synchronization and event processing
 * Must be called after slInitSystem() to enable peripheral reading
 */
extern void slInitSynch(void);

/**
 * Synchronize with vertical blank
 * Call once per frame for proper timing
 */
extern void slSynch(void);

/**
 * Turn display on
 */
extern void slTVOn(void);

/**
 * Turn display off
 */
extern void slTVOff(void);

/*============================================================================
 * VDP2 Color RAM Functions (Phase 1: Colored Text)
 *============================================================================*/

/**
 * Set Color RAM mode
 * @param mode - 0: 1024 colors RGB555, 1: 2048 colors RGB555, 2: 1024 colors RGB888
 */
extern void slColRAMMode(Uint16 mode);

/* NOTE: slSetColRAM does not exist in real SGL 3.02j.
 * Use direct CRAM writes (volatile uint16_t* to VDP2_COLRAM) instead.
 * See saturn_write_cram() in main_saturn.c for the canvas implementation. */

/*============================================================================
 * VDP2 Scroll Plane Functions (Phase 2: Rectangle Layer)
 *============================================================================*/

/**
 * Configure NBG0 character format
 * @param color_type - Color mode (COL_TYPE_16, COL_TYPE_256, etc.)
 * @param char_size  - Character size (CHAR_SIZE_1x1 or CHAR_SIZE_2x2)
 */
extern void slCharNbg0(Uint16 color_type, Uint16 char_size);

/**
 * Configure NBG1 character format
 */
extern void slCharNbg1(Uint16 color_type, Uint16 char_size);

/**
 * Set NBG0 page (pattern name data table)
 * @param cell_adr  Starting address of character patterns in VRAM
 * @param col_adr   Starting address of color palette (offset for 1-word mode, 0 = CRAM base)
 * @param data_type PNT data type (PNB_1WORD|CN_10BIT, PNB_1WORD|CN_12BIT, PNB_2WORD)
 */
extern void slPageNbg0(void *cell_adr, void *col_adr, Uint16 data_type);

/**
 * Set NBG1 page (pattern name data table)
 */
extern void slPageNbg1(void *cell_adr, void *col_adr, Uint16 data_type);

/**
 * Set scroll plane size (number of pages)
 * @param size - PL_SIZE_1x1, PL_SIZE_2x1, or PL_SIZE_2x2
 */
extern void slPlaneNbg0(Uint16 size);

/**
 * Set NBG0 scroll map page assignments
 * @param mapA - VRAM address of page A (top-left)
 * @param mapB - VRAM address of page B (top-right)
 * @param mapC - VRAM address of page C (bottom-left)
 * @param mapD - VRAM address of page D (bottom-right)
 */
extern void slMapNbg0(void *mapA, void *mapB, void *mapC, void *mapD);

/**
 * Set NBG0 scroll position
 * @param x - Horizontal scroll offset (16.16 fixed-point)
 * @param y - Vertical scroll offset (16.16 fixed-point)
 */
extern void slScrPosNbg0(FIXED x, FIXED y);
extern void slScrPosNbg2(FIXED x, FIXED y);
extern void slScrPosNbg3(FIXED x, FIXED y);

/**
 * Set NBG1 scroll position
 */
extern void slScrPosNbg1(FIXED x, FIXED y);

/**
 * Set scroll plane draw priority
 * @param screen - SGL screen index (scnNBG0, scnNBG1, etc.)
 * @param priority - Priority value (0-7, higher = drawn on top)
 */
extern void slPriority(Sint16 screen, Uint16 priority);

/**
 * Enable/disable scroll planes
 * @param flags - Bitmask of NBGxON flags
 */
extern void slScrAutoDisp(Uint32 flags);

/*============================================================================
 * VDP2 Background Functions (Phase 3: Efficient Screen Clear)
 *============================================================================*/

/**
 * Set the back screen color
 * @param addr - Back color address in CRAM
 * @param color - RGB555 color value
 */
extern void slBack1ColSet(void *addr, Uint16 color);

/*============================================================================
 * VDP2 Color Calculation Functions (Phase 5: Transparency)
 *============================================================================*/

/**
 * Set color calculation mode
 */
extern void slColorCalc(Uint16 mode);

/**
 * Enable color calculation on specific targets
 */
extern void slColorCalcOn(Uint16 target);

/**
 * Set color calculation (transparency) rate for a screen
 * @param screen - SGL screen index (scnNBG0, scnNBG1, etc.)
 * @param rate - Transparency rate (0-31, 31 = opaque)
 */
extern void slColRate(Sint16 screen, Uint16 rate);

/**
 * Color offset functions — control per-pixel color tinting
 */
extern void slColOffsetOn(Uint16 flag);
extern void slColOffsetOff(Uint16 flag);
extern void slColOffsetAUse(Uint16 screens);
extern void slColOffsetBUse(Uint16 screens);
extern void slColOffsetScrn(Uint16 screen, Uint16 offset_select);
extern void slColOffsetA(Sint16 r, Sint16 g, Sint16 b);
extern void slColOffsetB(Sint16 r, Sint16 g, Sint16 b);

/**
 * Set color calculation mode (distinct from slColorCalc)
 */
extern void slColorCalcMode(Uint16 mode);

/*============================================================================
 * VDP1 / Sprite Functions
 *============================================================================*/

/* Sprite priority: use slPrioritySpr0(num) macro defined below,
 * which calls slPriority(scnSPR0, num) — the standard SGL API. */

extern void slSpriteType(Uint16 type);

/**
 * Register a user callback for V-blank interrupt (runs BEFORE SGL processing).
 */
extern void slIntFunction(void (*func)());

/**
 * Register a user callback for V-blank synchronization (runs AFTER SGL processing).
 * Use this to patch VDP1 command table after SGL has written its commands.
 */
extern void slSynchFunction(void (*func)());

/**
 * Set VDP2 sprite color mode (SPCLMD bit in SPCTL register).
 * CRITICAL for VDP1 RGB555 rendering — without this, VDP2 interprets
 * VDP1's RGB555 pixels as palette data, producing garbage.
 *
 * @param mode - SPR_PAL (0) = palette only, SPR_PAL_RGB (1) = palette + RGB
 *
 * This sets SGL's internal shadow variable, so the setting persists
 * through slSynch() V-blank processing. Direct register writes do NOT
 * work because SGL overwrites SPCTL every frame.
 */
extern void slSpriteColMode(Uint16 mode);

/* Sprite color mode constants (from SL_DEF.H:608-609) */
#define SPR_PAL       0   /* Palette code only */
#define SPR_PAL_RGB   1   /* Use Palette and RGB */

/*============================================================================
 * VDP2 Constants
 *============================================================================*/

/* Character sizes */
#define CHAR_SIZE_1x1   0
#define CHAR_SIZE_2x2   1

/* Scroll plane sizes (page layout) */
#define PL_SIZE_1x1     0   /* 1 page wide x 1 page tall */
#define PL_SIZE_2x1     1   /* 2 pages wide x 1 page tall */
#define PL_SIZE_2x2     3   /* 2 pages wide x 2 pages tall */

/* Color modes for character planes */
#define COL_TYPE_16     0   /* 16-color palette */
#define COL_TYPE_256    1   /* 256-color palette */
#define COL_TYPE_2048   2   /* 2048-color palette */
#define COL_TYPE_32768  3   /* 32768 direct color */
#define COL_TYPE_16M    4   /* 16M direct color */

/* Scroll plane enable flags */
#define NBG0ON          (1 << 0)
#define NBG1ON          (1 << 1)
#define NBG2ON          (1 << 2)
#define NBG3ON          (1 << 3)
#define RBG0ON          (1 << 4)
#define SPRON           (1 << 5)
#define BACKON          (1 << 6)

/* WARNING: These are SGL-internal dispatch indices, NOT VDP2 layer numbers.
 * The values are intentionally non-sequential. Do not "correct" them.
 * See SL_DEF.H lines 647-668 in the official SGL 3.02j SDK. */
#define scnNBG0         1
#define scnNBG1         0
#define scnNBG2         3
#define scnNBG3         2
#define scnRBG0         5

/* Sprite register indices (negative = SGL convention for sprite regs) */
#define scnSPR0         (-7)
#define scnSPR1         (-8)

/**
 * Shadow enable per screen (pass 0 to disable all)
 */
extern void slShadowOn(Uint16 flag);

/**
 * Scroll plane zoom (coordinate increment)
 * Values are FIXED (16.16): toFIXED(1) = normal, toFIXED(1)/2 = 2x enlarge
 */
extern void slZoomNbg0(FIXED x, FIXED y);
extern void slZoomNbg1(FIXED x, FIXED y);
extern void slZoomMode(Uint16 screen, Uint16 mode);
#define slZoomModeNbg0(mode)  slZoomMode(scnNBG0, mode)
#define slZoomModeNbg1(mode)  slZoomMode(scnNBG1, mode)

/* Zoom mode constants */
#define ZOOM_1        0
#define ZOOM_HALF     1
#define ZOOM_QUARTER  2

/**
 * VDP2 rectangular window position
 */
extern void slScrWindow0(Uint16 left, Uint16 top, Uint16 right, Uint16 bottom);
extern void slScrWindow1(Uint16 left, Uint16 top, Uint16 right, Uint16 bottom);

/**
 * VDP2 per-plane window mode
 */
extern void slScrWindowMode(Uint16 screen, Uint16 mode);
#define slScrWindowModeNbg0(mode)  slScrWindowMode(scnNBG0, mode)
#define slScrWindowModeNbg1(mode)  slScrWindowMode(scnNBG1, mode)
#define slScrWindowModeNbg2(mode)  slScrWindowMode(scnNBG2, mode)
#define slScrWindowModeNbg3(mode)  slScrWindowMode(scnNBG3, mode)
#define slScrWindowModeRbg0(mode)  slScrWindowMode(scnRBG0, mode)
#define slScrWindowModeSPR(mode)   slScrWindowMode(scnSPR, mode)
#define slScrWindowModeCCAL(mode)  slScrWindowMode(scnCCAL, mode)

/* SGL screen indices for sprites/rotation/color calc */
#define scnSPR   4
#define scnROT   7
#define scnCCAL  6

/* Window control flags */
#define win_OR    0x80
#define win_AND   0x00
#define win0_IN   0x03
#define win0_OUT  0x02
#define win1_IN   0x0c
#define win1_OUT  0x08
#define spw_IN    0x30
#define spw_OUT   0x20

/**
 * VDP2 line scroll mode and table address
 */
extern void slLineScrollMode(Uint16 screen, Uint16 mode);
extern void slLineScrollTable0(void* addr);
extern void slLineScrollTable1(void* addr);
#define slLineScrollModeNbg0(mode)  slLineScrollMode(scnNBG0, mode)
#define slLineScrollModeNbg1(mode)  slLineScrollMode(scnNBG1, mode)

/* Line scroll flags */
#define VCellScroll   0x01
#define lineHScroll   0x02
#define lineVScroll   0x04
#define lineZoom      0x08
#define lineSZ1       0x00
#define lineSZ2       0x10
#define lineSZ4       0x20
#define lineSZ8       0x30

/* Convenience macros (match real SGL 3.02j SL_DEF.H) */
#define slPriorityNbg0(num)   slPriority(scnNBG0, num)
#define slPriorityNbg1(num)   slPriority(scnNBG1, num)
#define slPriorityNbg2(num)   slPriority(scnNBG2, num)
#define slPriorityNbg3(num)   slPriority(scnNBG3, num)
#define slPrioritySpr0(num)   slPriority(scnSPR0, num)
#define slColRateNbg0(rate)   slColRate(scnNBG0, rate)
#define slColRateNbg1(rate)   slColRate(scnNBG1, rate)

/* Color calculation constants */
#define CC_RATE         (1 << 0)
#define CC_2ND          (1 << 1)
#define CC_NBG0         (1 << 4)
#define CC_NBG1         (1 << 5)

/* Color RAM modes */
#define CRAM_MODE_0     0   /* 1024 colors, RGB555 */
#define CRAM_MODE_1     1   /* 2048 colors, RGB555 */
#define CRAM_MODE_2     2   /* 1024 colors, RGB888 */

/* VDP2 VRAM base addresses */
#define VDP2_VRAM_A0    0x25E00000
#define VDP2_VRAM_A1    0x25E20000
#define VDP2_VRAM_B0    0x25E40000
#define VDP2_VRAM_B1    0x25E60000

/* VDP2 Color RAM base address */
#define VDP2_COLRAM     0x25F00000

/* Color RAM mode parameters (for slColRAMMode) */
#define CRM16_1024      0   /* Mode 0: 1024 colors, RGB555 */
#define CRM16_2048      1   /* Mode 1: 2048 colors, RGB555 (SGL default) */
#define CRM32_1024      2   /* Mode 2: 1024 colors, RGB888 */

/* Page/PNT configuration (for slPageNbg0 etc.) */
#define PNB_1WORD       0x8000
#define PNB_2WORD       0
#define CN_10BIT        0
#define CN_12BIT        0x4000

/*============================================================================
 * SMPC RTC and Status (from SL_DEF.H)
 *============================================================================*/

typedef struct {
    Uint16 year;
    Uint8  month;     /* bits[7:4]=weekday, bits[3:0]=month */
    Uint8  date;
    Uint8  hour;
    Uint8  minute;
    Uint8  second;
    Uint8  dummy;
} SmpcDateTime;

typedef struct {
    Uint8        cond;
    Uint8        dummy1;
    Uint16       dummy2;
    SmpcDateTime rtc;
    Uint8        ctg;
    Uint8        area;
    Uint16       system;
    Uint32       smem;
} SmpcStatus;

extern SmpcStatus* Smpc_Status;

/*============================================================================
 * SMPC Commands
 *============================================================================*/

#define SMPC_RESENA   0x0b
#define SMPC_RESDIS   0x0c
#define SMPC_GETSTS   0x0d
#define SMPC_NO_WAIT  0x00

extern Bool slRequestCommand(Uint8 cmd, Uint8 wait);
extern Uint32 slDec2Hex(Uint32 bcd);

#define slResetEnable()   slRequestCommand(SMPC_RESENA, SMPC_NO_WAIT)
#define slResetDisable()  slRequestCommand(SMPC_RESDIS, SMPC_NO_WAIT)
#define slGetStatus()     slRequestCommand(SMPC_GETSTS, SMPC_NO_WAIT)

#endif /* SGL_DEFS_H */
