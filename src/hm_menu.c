#include "global.h"
#include "bg.h"
#include "data.h"
#include "event_data.h"
#include "field_player_avatar.h"
#include "fldeff.h"
#include "gpu_regs.h"
#include "hm_menu.h"
#include "main.h"
#include "malloc.h"
#include "menu.h"
#include "overworld.h"
#include "palette.h"
#include "party_menu.h"
#include "region_map.h"
#include "sound.h"
#include "sprite.h"
#include "strings.h"
#include "task.h"
#include "text.h"
#include "text_window.h"
#include "window.h"
#include "constants/characters.h"
#include "constants/moves.h"
#include "constants/party_menu.h"
#include "constants/rgb.h"
#include "constants/songs.h"

#define HM_MENU_COUNT (FIELD_MOVE_WATERFALL + 1) // Cut, Flash, Rock Smash, Strength, Surf, Fly, Dive, Waterfall
#define HM_MENU_COLUMNS 2
#define HM_MENU_ROWS ((HM_MENU_COUNT + HM_MENU_COLUMNS - 1) / HM_MENU_COLUMNS)
#define HM_MENU_OPTION_WIDTH 104

// Matches the fixed constants WindowFunc_DrawStandardFrame hardcodes
// internally (src/menu.c: STD_WINDOW_BASE_TILE_NUM/STD_WINDOW_PALETTE_NUM) -
// any window that wants the game's standard bordered-box look must load its
// frame graphics at this exact tile offset/palette bank.
#define HM_STD_FRAME_BASE_TILE 0x214
#define HM_STD_FRAME_PALETTE_NUM 14

enum
{
    WIN_HM_HEADER,
    WIN_HM_LIST
};

struct HMMenuState
{
    u8 state;
};

static EWRAM_DATA struct HMMenuState *sHMMenu = NULL;
static EWRAM_DATA MainCallback sHMMenuExitCallback = NULL;

static void CB2_InitHMMenu(void);
static void VBlankCB_HMMenu(void);
static void CB2_HMMenu(void);
static void Task_HMMenu(u8 taskId);
static void PrintHMMenuHeaderText(const u8 *str);
static void PrintHMMenuList(void);
static void HMMenu_TryUseSelection(u8 taskId, u8 fieldMove);
static void CloseHMMenu(u8 taskId);

static const struct BgTemplate sHMMenuBgTemplates[] = {
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

static const struct WindowTemplate sHMMenuWinTemplates[] = {
    [WIN_HM_HEADER] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 1,
        .width = 26,
        .height = 2,
        .paletteNum = HM_STD_FRAME_PALETTE_NUM,
        .baseBlock = 1
    },
    [WIN_HM_LIST] = {
        .bg = 0,
        .tilemapLeft = 2,
        .tilemapTop = 4,
        .width = 26,
        .height = HM_MENU_ROWS * 2,
        .paletteNum = HM_STD_FRAME_PALETTE_NUM,
        .baseBlock = 0x40
    },
    DUMMY_WIN_TEMPLATE
};

static const u8 sHMMenuAvailableColor[3] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_DARK_GRAY, TEXT_COLOR_LIGHT_GRAY};
static const u8 sHMMenuUnavailableColor[3] = {TEXT_COLOR_TRANSPARENT, TEXT_COLOR_LIGHT_GRAY, TEXT_COLOR_LIGHT_GRAY};

// FIELD_MOVE_CUT..FIELD_MOVE_WATERFALL order
static const u16 sHMMenuMoveIds[HM_MENU_COUNT] = {
    [FIELD_MOVE_CUT]        = MOVE_CUT,
    [FIELD_MOVE_FLASH]      = MOVE_FLASH,
    [FIELD_MOVE_ROCK_SMASH] = MOVE_ROCK_SMASH,
    [FIELD_MOVE_STRENGTH]   = MOVE_STRENGTH,
    [FIELD_MOVE_SURF]       = MOVE_SURF,
    [FIELD_MOVE_FLY]        = MOVE_FLY,
    [FIELD_MOVE_DIVE]       = MOVE_DIVE,
    [FIELD_MOVE_WATERFALL]  = MOVE_WATERFALL,
};

// Re-runs the same position/context check the party menu field-move list used
// to use (facing a cuttable tree, facing surfable water, in a dark cave, etc).
// No party Pokemon needs to know the move for this check to succeed.
static bool8 (*const sHMMenuSetUpFuncs[HM_MENU_COUNT])(void) = {
    [FIELD_MOVE_CUT]        = SetUpFieldMove_Cut,
    [FIELD_MOVE_FLASH]      = SetUpFieldMove_Flash,
    [FIELD_MOVE_ROCK_SMASH] = SetUpFieldMove_RockSmash,
    [FIELD_MOVE_STRENGTH]   = SetUpFieldMove_Strength,
    [FIELD_MOVE_SURF]       = SetUpFieldMove_Surf,
    [FIELD_MOVE_FLY]        = SetUpFieldMove_Fly,
    [FIELD_MOVE_DIVE]       = SetUpFieldMove_Dive,
    [FIELD_MOVE_WATERFALL]  = SetUpFieldMove_Waterfall,
};

static const u8 *const sHMMenuContextFailMessages[HM_MENU_COUNT] = {
    [FIELD_MOVE_CUT]        = gText_NothingToCut,
    [FIELD_MOVE_FLASH]      = gText_CantUseHere,
    [FIELD_MOVE_ROCK_SMASH] = gText_CantUseHere,
    [FIELD_MOVE_STRENGTH]   = gText_CantUseHere,
    [FIELD_MOVE_SURF]       = gText_CantSurfHere,
    [FIELD_MOVE_FLY]        = gText_CantUseHere,
    [FIELD_MOVE_DIVE]       = gText_CantUseHere,
    [FIELD_MOVE_WATERFALL]  = gText_CantUseHere,
};

void CB2_HMMenuFromStartMenu(void)
{
    sHMMenu = AllocZeroed(sizeof(struct HMMenuState));
    SetMainCallback2(CB2_HMMenu);
}

static void VBlankCB_HMMenu(void)
{
    LoadOam();
    ProcessSpriteCopyRequests();
    TransferPlttBuffer();
}

static void CB2_InitHMMenu(void)
{
    RunTasks();
    AnimateSprites();
    BuildOamBuffer();
    UpdatePaletteFade();
}

static void CB2_HMMenu(void)
{
    switch (sHMMenu->state)
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
        InitBgsFromTemplates(0, sHMMenuBgTemplates, NELEMS(sHMMenuBgTemplates));
        ChangeBgX(0, 0, BG_COORD_SET);
        ChangeBgY(0, 0, BG_COORD_SET);
        InitWindows(sHMMenuWinTemplates);
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
        LoadUserWindowBorderGfx(WIN_HM_HEADER, HM_STD_FRAME_BASE_TILE, BG_PLTT_ID(HM_STD_FRAME_PALETTE_NUM));
        Menu_LoadStdPal();
        break;
    case 4:
        DrawStdWindowFrame(WIN_HM_HEADER, TRUE);
        DrawStdWindowFrame(WIN_HM_LIST, TRUE);
        break;
    case 5:
        PrintHMMenuHeaderText(gText_MenuUseHM);
        PrintHMMenuList();
        break;
    default:
        SetVBlankCallback(VBlankCB_HMMenu);
        BeginNormalPaletteFade(PALETTES_ALL, 0, 0x10, 0, RGB_BLACK);
        CreateTask(Task_HMMenu, 0);
        SetMainCallback2(CB2_InitHMMenu);
        return;
    }
    sHMMenu->state++;
}

static void PrintHMMenuHeaderText(const u8 *str)
{
    FillWindowPixelBuffer(WIN_HM_HEADER, PIXEL_FILL(1));
    AddTextPrinterParameterized3(WIN_HM_HEADER, FONT_NORMAL, 8, 1, sHMMenuAvailableColor, TEXT_SKIP_DRAW, str);
    CopyWindowToVram(WIN_HM_HEADER, COPYWIN_GFX);
}

static void PrintHMMenuList(void)
{
    u8 i;

    FillWindowPixelBuffer(WIN_HM_LIST, PIXEL_FILL(1));
    for (i = 0; i < HM_MENU_COUNT; i++)
    {
        const u8 *color = (CanUseHMFieldMove(i) == TRUE) ? sHMMenuAvailableColor : sHMMenuUnavailableColor;
        u8 x = (HM_MENU_OPTION_WIDTH * (i % HM_MENU_COLUMNS)) + 8;
        u8 y = (16 * (i / HM_MENU_COLUMNS)) + 1;
        AddTextPrinterParameterized3(WIN_HM_LIST, FONT_NORMAL, x, y, color, TEXT_SKIP_DRAW, gMoveNames[sHMMenuMoveIds[i]]);
    }
    InitMenuActionGrid(WIN_HM_LIST, HM_MENU_OPTION_WIDTH, HM_MENU_COLUMNS, HM_MENU_ROWS, 0);
    CopyWindowToVram(WIN_HM_LIST, COPYWIN_GFX);
}

static void Task_HMMenu(u8 taskId)
{
    switch (gTasks[taskId].data[0])
    {
    case 0:
        if (gPaletteFade.active)
            return;
        gTasks[taskId].data[0]++;
        break;
    case 1:
    {
        s8 selection = Menu_ProcessGridInput();

        if (JOY_NEW(DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT))
            CopyWindowToVram(WIN_HM_LIST, COPYWIN_GFX);

        if (selection == MENU_B_PRESSED)
        {
            sHMMenuExitCallback = CB2_ReturnToFieldWithOpenMenu;
            BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
            gTasks[taskId].data[0]++;
        }
        else if (selection != MENU_NOTHING_CHOSEN)
        {
            HMMenu_TryUseSelection(taskId, selection);
        }
        break;
    }
    case 2:
        if (gPaletteFade.active)
            return;
        CloseHMMenu(taskId);
        break;
    }
}

static void HMMenu_TryUseSelection(u8 taskId, u8 fieldMove)
{
    if (FlagGet(FLAG_BADGE01_GET + fieldMove) != TRUE)
    {
        PrintHMMenuHeaderText(gText_HMMenuNeedBadge);
        return;
    }
    if (CanUseHMFieldMove(fieldMove) != TRUE)
    {
        PrintHMMenuHeaderText(gText_HMItemNotInBag);
        return;
    }
    if (sHMMenuSetUpFuncs[fieldMove]() != TRUE)
    {
        PrintHMMenuHeaderText(sHMMenuContextFailMessages[fieldMove]);
        return;
    }

    // SetUpFieldMove_X already queued gFieldCallback2/gPostMenuFieldCallback
    // (or, for Fly, nothing - it just opens the destination map screen).
    sHMMenuExitCallback = (fieldMove == FIELD_MOVE_FLY) ? CB2_OpenFlyMap : CB2_ReturnToField;
    BeginNormalPaletteFade(PALETTES_ALL, 0, 0, 0x10, RGB_BLACK);
    gTasks[taskId].data[0]++;
}

static void CloseHMMenu(u8 taskId)
{
    FreeAllWindowBuffers();
    FREE_AND_SET_NULL(sHMMenu);
    DestroyTask(taskId);
    SetMainCallback2(sHMMenuExitCallback);
}
