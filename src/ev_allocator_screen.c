#include "global.h"
#include "malloc.h"
#include "bg.h"
#include "gpu_regs.h"
#include "palette.h"
#include "main.h"
#include "sprite.h"
#include "task.h"
#include "text.h"
#include "string_util.h"
#include "window.h"
#include "menu.h"
#include "text_window.h"
#include "overworld.h"
#include "event_data.h"
#include "pokemon.h"
#include "strings.h"
#include "sound.h"
#include "ev_allocator_screen.h"
#include "constants/songs.h"
#include "constants/pokemon.h"
#include "constants/rgb.h"

// Matches the literal constants src/menu.c uses locally for its own
// STD_WINDOW_BASE_TILE_NUM / STD_WINDOW_PALETTE_NUM (not exported from that
// file) - any window using the game's standard bordered-box frame needs its
// graphics loaded at this exact tile offset/palette bank, since
// WindowFunc_DrawStandardFrame hardcodes them internally regardless of the
// window template's own paletteNum.
#define EV_STD_FRAME_BASE_TILE 0x214
#define EV_STD_FRAME_PALETTE_NUM 14

enum
{
    EVROW_HP,
    EVROW_ATK,
    EVROW_DEF,
    EVROW_SPEED,
    EVROW_SPATK,
    EVROW_SPDEF,
    EVROW_CONFIRM,
    EVROW_CANCEL,
    EVROW_COUNT
};

#define EV_BAR_X      60
#define EV_BAR_WIDTH  96
#define EV_BAR_HEIGHT 6
#define EV_ROW_Y(i)   (22 + (i) * 16)

struct EVAllocatorState
{
    struct Pokemon *mon;
    u8 workingEvs[NUM_STATS];
    u16 totalEvs;
    u8 selection;
    u8 windowId;
    u8 state;
};

static EWRAM_DATA struct EVAllocatorState *sEVAllocator = NULL;

static void CB2_InitEVAllocator(void);
static void VBlankCB_EVAllocator(void);
static void CB2_EVAllocator(void);
static void Task_EVAllocator(u8 taskId);
static u8 EVAllocator_ProcessInput(void);
static void DrawEVAllocatorScreen(void);
static void CloseEVAllocatorScreen(u8 taskId, bool8 apply);

static const struct BgTemplate sEVAllocatorBgTemplates[] =
{
    {
        .bg = 0,
        .charBaseIndex = 0,
        .mapBaseIndex = 31,
        .screenSize = 0,
        .paletteMode = 0,
        .priority = 0,
        .baseTile = 0
    },
};

static const struct WindowTemplate sEVAllocatorWinTemplates[] =
{
    {
        .bg = 0,
        .tilemapLeft = 1,
        .tilemapTop = 1,
        .width = 28,
        .height = 18,
        .paletteNum = EV_STD_FRAME_PALETTE_NUM,
        .baseBlock = 1
    },
    DUMMY_WIN_TEMPLATE
};

static const u8 sEVAllocatorHeaderColor[3] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};
static const u8 sEVAllocatorCursorColor[3] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_RED, TEXT_COLOR_LIGHT_RED};

static const u8 sText_EVAllocatorTitle[] = _("EV ALLOCATOR");
static const u8 sText_Remaining[] = _("REMAINING");
static const u8 sText_Confirm[] = _("CONFIRM");
static const u8 sText_Cancel[] = _("CANCEL");
static const u8 sText_Cursor[] = _(">");

static const u8 *const sEVStatLabels[NUM_STATS] =
{
    [STAT_HP]    = gText_HP3,
    [STAT_ATK]   = gText_Attack,
    [STAT_DEF]   = gText_Defense,
    [STAT_SPEED] = gText_Speed,
    [STAT_SPATK] = gText_SpAtk,
    [STAT_SPDEF] = gText_SpDef,
};

void OpenEVAllocatorScreen(void)
{
    u8 i;

    sEVAllocator = AllocZeroed(sizeof(struct EVAllocatorState));
    sEVAllocator->mon = &gPlayerParty[gSpecialVar_0x8004];
    sEVAllocator->selection = EVROW_HP;
    sEVAllocator->state = 0;
    sEVAllocator->totalEvs = 0;

    for (i = 0; i < NUM_STATS; i++)
    {
        sEVAllocator->workingEvs[i] = GetMonData(sEVAllocator->mon, MON_DATA_HP_EV + i, NULL);
        sEVAllocator->totalEvs += sEVAllocator->workingEvs[i];
    }

    SetMainCallback2(CB2_EVAllocator);
}

static void VBlankCB_EVAllocator(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void CB2_InitEVAllocator(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void CB2_EVAllocator(void)
{
    switch (sEVAllocator->state)
    {
    case 0:
        SetVBlankCallback(NULL);
        SetHBlankCallback(NULL);
        break;
    case 1:
        DmaClearLarge16(3, (void *)VRAM, VRAM_SIZE, 0x1000);
        DmaClear32(3, (void *)OAM, OAM_SIZE);
        DmaClear16(3, (void *)PLTT, PLTT_SIZE);
        SetGpuReg(REG_OFFSET_DISPCNT, DISPCNT_MODE_0);
        ResetBgsAndClearDma3BusyFlags(0);
        InitBgsFromTemplates(0, sEVAllocatorBgTemplates, NELEMS(sEVAllocatorBgTemplates));
        ChangeBgX(0, 0, BG_COORD_SET);
        ChangeBgY(0, 0, BG_COORD_SET);
        InitWindows(sEVAllocatorWinTemplates);
        DeactivateAllTextPrinters();
        ShowBg(0);
        break;
    case 2:
        ResetSpriteData();
        ResetPaletteFade();
        FreeAllSpritePalettes();
        ResetTasks();
        break;
    case 3:
        sEVAllocator->windowId = 0;
        LoadUserWindowBorderGfx(sEVAllocator->windowId, EV_STD_FRAME_BASE_TILE, BG_PLTT_ID(EV_STD_FRAME_PALETTE_NUM));
        Menu_LoadStdPal();
        break;
    case 4:
        DrawStdWindowFrame(sEVAllocator->windowId, TRUE);
        break;
    case 5:
        DrawEVAllocatorScreen();
        break;
    default:
        SetVBlankCallback(VBlankCB_EVAllocator);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0x10, 0, RGB_BLACK);
        CreateTask(Task_EVAllocator, 0);
        SetMainCallback2(CB2_InitEVAllocator);
        return;
    }
    sEVAllocator->state++;
}

static void Task_EVAllocator(u8 taskId)
{
    switch (gTasks[taskId].data[0])
    {
    case 0:
        if (gPaletteFade.active)
            return;
        gTasks[taskId].data[0]++;
        break;
    case 1:
        switch (EVAllocator_ProcessInput())
        {
        case 1:
        case 2:
            DrawEVAllocatorScreen();
            break;
        case 3: // Confirm
            gTasks[taskId].data[1] = TRUE;
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
            gTasks[taskId].data[0]++;
            break;
        case 4: // Cancel
            gTasks[taskId].data[1] = FALSE;
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
            gTasks[taskId].data[0]++;
            break;
        }
        break;
    case 2:
        if (gPaletteFade.active)
            return;
        CloseEVAllocatorScreen(taskId, gTasks[taskId].data[1]);
        break;
    }
}

static u8 EVAllocator_ProcessInput(void)
{
    if (JOY_NEW(DPAD_UP))
    {
        sEVAllocator->selection = (sEVAllocator->selection == 0) ? EVROW_CANCEL : sEVAllocator->selection - 1;
        PlaySE(SE_SELECT);
        return 1;
    }
    else if (JOY_NEW(DPAD_DOWN))
    {
        sEVAllocator->selection = (sEVAllocator->selection == EVROW_CANCEL) ? 0 : sEVAllocator->selection + 1;
        PlaySE(SE_SELECT);
        return 1;
    }
    else if (sEVAllocator->selection < NUM_STATS && JOY_REPEAT(DPAD_RIGHT))
    {
        u8 *ev = &sEVAllocator->workingEvs[sEVAllocator->selection];
        if (*ev < MAX_PER_STAT_EVS && sEVAllocator->totalEvs < MAX_TOTAL_EVS)
        {
            (*ev)++;
            sEVAllocator->totalEvs++;
            PlaySE(SE_SELECT);
            return 2;
        }
        return 0;
    }
    else if (sEVAllocator->selection < NUM_STATS && JOY_REPEAT(DPAD_LEFT))
    {
        u8 *ev = &sEVAllocator->workingEvs[sEVAllocator->selection];
        if (*ev > 0)
        {
            (*ev)--;
            sEVAllocator->totalEvs--;
            PlaySE(SE_SELECT);
            return 2;
        }
        return 0;
    }
    else if (JOY_NEW(A_BUTTON))
    {
        if (sEVAllocator->selection == EVROW_CONFIRM)
            return 3;
        else if (sEVAllocator->selection == EVROW_CANCEL)
            return 4;
        return 0;
    }
    else if (JOY_NEW(B_BUTTON))
    {
        return 4;
    }
    return 0;
}

static void DrawEVAllocatorScreen(void)
{
    u8 windowId = sEVAllocator->windowId;
    u8 buf[16];
    u8 i;

    FillWindowPixelBuffer(windowId, PIXEL_FILL(TEXT_COLOR_WHITE));

    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 4, 2, sEVAllocatorHeaderColor, TEXT_SKIP_DRAW, sText_EVAllocatorTitle);

    AddTextPrinterParameterized3(windowId, FONT_SMALL, 130, 4, sEVAllocatorHeaderColor, TEXT_SKIP_DRAW, sText_Remaining);
    ConvertIntToDecimalStringN(buf, MAX_TOTAL_EVS - sEVAllocator->totalEvs, STR_CONV_MODE_RIGHT_ALIGN, 3);
    AddTextPrinterParameterized3(windowId, FONT_SMALL, 190, 4, sEVAllocatorHeaderColor, TEXT_SKIP_DRAW, buf);

    for (i = 0; i < NUM_STATS; i++)
    {
        u16 y = EV_ROW_Y(i);
        u16 fillWidth = (u16)(EV_BAR_WIDTH * sEVAllocator->workingEvs[i]) / MAX_PER_STAT_EVS;

        if (sEVAllocator->selection == i)
            AddTextPrinterParameterized3(windowId, FONT_NORMAL, 0, y, sEVAllocatorCursorColor, TEXT_SKIP_DRAW, sText_Cursor);

        AddTextPrinterParameterized3(windowId, FONT_NORMAL, 8, y, sEVAllocatorHeaderColor, TEXT_SKIP_DRAW, sEVStatLabels[i]);

        FillWindowPixelRect(windowId, TEXT_COLOR_LIGHT_GRAY, EV_BAR_X, y + 1, EV_BAR_WIDTH, EV_BAR_HEIGHT);
        if (fillWidth > 0)
            FillWindowPixelRect(windowId, TEXT_COLOR_GREEN, EV_BAR_X, y + 1, fillWidth, EV_BAR_HEIGHT);

        ConvertIntToDecimalStringN(buf, sEVAllocator->workingEvs[i], STR_CONV_MODE_RIGHT_ALIGN, 3);
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, EV_BAR_X + EV_BAR_WIDTH + 8, y, sEVAllocatorHeaderColor, TEXT_SKIP_DRAW, buf);
    }

    if (sEVAllocator->selection == EVROW_CONFIRM)
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, 100, EV_ROW_Y(NUM_STATS) + 8, sEVAllocatorCursorColor, TEXT_SKIP_DRAW, sText_Cursor);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 108, EV_ROW_Y(NUM_STATS) + 8, sEVAllocatorHeaderColor, TEXT_SKIP_DRAW, sText_Confirm);

    if (sEVAllocator->selection == EVROW_CANCEL)
        AddTextPrinterParameterized3(windowId, FONT_NORMAL, 172, EV_ROW_Y(NUM_STATS) + 8, sEVAllocatorCursorColor, TEXT_SKIP_DRAW, sText_Cursor);
    AddTextPrinterParameterized3(windowId, FONT_NORMAL, 180, EV_ROW_Y(NUM_STATS) + 8, sEVAllocatorHeaderColor, TEXT_SKIP_DRAW, sText_Cancel);

    PutWindowTilemap(windowId);
    CopyWindowToVram(windowId, COPYWIN_FULL);
}

static void CloseEVAllocatorScreen(u8 taskId, bool8 apply)
{
    if (apply)
    {
        u8 i;
        for (i = 0; i < NUM_STATS; i++)
            SetMonData(sEVAllocator->mon, MON_DATA_HP_EV + i, &sEVAllocator->workingEvs[i]);
        CalculateMonStats(sEVAllocator->mon);
    }

    gSpecialVar_Result = apply;
    FreeAllWindowBuffers();
    FREE_AND_SET_NULL(sEVAllocator);
    DestroyTask(taskId);
    SetMainCallback2(CB2_ReturnToFieldContinueScript);
}
