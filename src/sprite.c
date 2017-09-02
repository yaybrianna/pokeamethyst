#include "global.h"
#include "sprite.h"
#include "main.h"

#define MAX_SPRITE_COPY_REQUESTS 64

#define OAM_MATRIX_COUNT 32

#define SET_SPRITE_TILE_RANGE(index, start, count) \
{                                                  \
    sSpriteTileRanges[index * 2] = start;          \
    (sSpriteTileRanges + 1)[index * 2] = count;    \
}

#define ALLOC_SPRITE_TILE(n)                             \
{                                                        \
    gSpriteTileAllocBitmap[(n) / 8] |= (1 << ((n) % 8)); \
}

#define FREE_SPRITE_TILE(n)                               \
{                                                         \
    gSpriteTileAllocBitmap[(n) / 8] &= ~(1 << ((n) % 8)); \
}

#define SPRITE_TILE_IS_ALLOCATED(n) ((gSpriteTileAllocBitmap[(n) / 8] >> ((n) % 8)) & 1)


struct SpriteCopyRequest
{
    const u8 *src;
    u8 *dest;
    u16 size;
};

// this file's functions
void UpdateOamCoords(void);
void BuildSpritePriorities(void);
void SortSprites(void);
void CopyMatricesToOamBuffer(void);
void AddSpritesToOamBuffer(void);
u8 CreateSpriteAt(u8 index, const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority);
void ClearSpriteCopyRequests(void);
void ResetOamMatrices(void);
void ResetSprite(struct Sprite *sprite);
s16 AllocSpriteTiles(u16 tileCount);
void RequestSpriteFrameImageCopy(u16 index, u16 tileNum, const struct SpriteFrameImage *images);
void ResetAllSprites(void);
void BeginAnim(struct Sprite *sprite);
void ContinueAnim(struct Sprite *sprite);
void AnimCmd_frame(struct Sprite *sprite);
void AnimCmd_end(struct Sprite *sprite);
void AnimCmd_jump(struct Sprite *sprite);
void AnimCmd_loop(struct Sprite *sprite);
void BeginAnimLoop(struct Sprite *sprite);
void ContinueAnimLoop(struct Sprite *sprite);
void JumpToTopOfAnimLoop(struct Sprite *sprite);
void BeginAffineAnim(struct Sprite *sprite);
void ContinueAffineAnim(struct Sprite *sprite);
void AffineAnimDelay(u8 matrixNum, struct Sprite *sprite);
void AffineAnimCmd_loop(u8 matrixNum, struct Sprite *sprite);
void BeginAffineAnimLoop(u8 matrixNum, struct Sprite *sprite);
void ContinueAffineAnimLoop(u8 matrixNum, struct Sprite *sprite);
void JumpToTopOfAffineAnimLoop(u8 matrixNum, struct Sprite *sprite);
void AffineAnimCmd_jump(u8 matrixNum, struct Sprite *sprite);
void AffineAnimCmd_end(u8 matrixNum, struct Sprite *sprite);
void AffineAnimCmd_frame(u8 matrixNum, struct Sprite *sprite);
void CopyOamMatrix(u8 destMatrixIndex, struct OamMatrix *srcMatrix);
u8 GetSpriteMatrixNum(struct Sprite *sprite);
void SetSpriteOamFlipBits(struct Sprite *sprite, u8 hFlip, u8 vFlip);
void AffineAnimStateRestartAnim(u8 matrixNum);
void AffineAnimStateStartAnim(u8 matrixNum, u8 animNum);
void AffineAnimStateReset(u8 matrixNum);
void ApplyAffineAnimFrameAbsolute(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd);
void DecrementAnimDelayCounter(struct Sprite *sprite);
bool8 DecrementAffineAnimDelayCounter(struct Sprite *sprite, u8 matrixNum);
void ApplyAffineAnimFrameRelativeAndUpdateMatrix(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd);
s16 ConvertScaleParam(s16 scale);
void GetAffineAnimFrame(u8 matrixNum, struct Sprite *sprite, struct AffineAnimFrameCmd *frameCmd);
void ApplyAffineAnimFrame(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd);
void ResetAffineAnimData(void);
u8 IndexOfSpriteTileTag(u16 tag);
void AllocSpriteTileRange(u16 tag, u16 start, u16 count);
void DoLoadSpritePalette(const u16 *src, u16 paletteOffset);
void obj_update_pos2(struct Sprite* sprite, s32 a1, s32 a2);

typedef void (*AnimFunc)(struct Sprite *);
typedef void (*AnimCmdFunc)(struct Sprite *);
typedef void (*AffineAnimCmdFunc)(u8 matrixNum, struct Sprite *);

extern struct AffineAnimState sAffineAnimStates[OAM_MATRIX_COUNT];
extern u32 gOamMatrixAllocBitmap;

EWRAM_DATA struct Sprite gSprites[MAX_SPRITES + 1] = {0};
EWRAM_DATA u16 gSpritePriorities[MAX_SPRITES] = {0};
EWRAM_DATA u8 gSpriteOrder[MAX_SPRITES] = {0};
EWRAM_DATA bool8 gShouldProcessSpriteCopyRequests = 0;
EWRAM_DATA u8 gSpriteCopyRequestCount = 0;
EWRAM_DATA struct SpriteCopyRequest gSpriteCopyRequests[MAX_SPRITES] = {0};
EWRAM_DATA u8 gOamLimit = 0;
EWRAM_DATA u16 gReservedSpriteTileCount = 0;
EWRAM_DATA u8 gSpriteTileAllocBitmap[128] = {0};
EWRAM_DATA s16 gSpriteCoordOffsetX = 0;
EWRAM_DATA s16 gSpriteCoordOffsetY = 0;
EWRAM_DATA struct OamMatrix gOamMatrices[OAM_MATRIX_COUNT] = {0};
EWRAM_DATA bool8 gAffineAnimsDisabled = 0;

extern const struct OamData gDummyOamData;
extern const struct SpriteTemplate gDummySpriteTemplate;
extern const struct Sprite sDummySprite;
extern const u8 sCenterToCornerVecTable[3][4][2];
extern const AnimFunc sAnimFuncs[];
extern const AnimFunc sAffineAnimFuncs[];
extern const AnimCmdFunc sAnimCmdFuncs[];
extern const AffineAnimCmdFunc sAffineAnimCmdFuncs[];
extern const s32 gUnknown_082EC6F4[];

void ResetSpriteData(void)
{
    ResetOamRange(0, 128);
    ResetAllSprites();
    ClearSpriteCopyRequests();
    ResetAffineAnimData();
    FreeSpriteTileRanges();
    gOamLimit = 64;
    gReservedSpriteTileCount = 0;
    AllocSpriteTiles(0);
    gSpriteCoordOffsetX = 0;
    gSpriteCoordOffsetY = 0;
}

void AnimateSprites(void)
{
    u8 i;
    for (i = 0; i < MAX_SPRITES; i++)
    {
        struct Sprite *sprite = &gSprites[i];

        if (sprite->inUse)
        {
            sprite->callback(sprite);

            if (sprite->inUse)
                AnimateSprite(sprite);
        }
    }
}

void BuildOamBuffer(void)
{
    u8 temp;
    UpdateOamCoords();
    BuildSpritePriorities();
    SortSprites();
    temp = gMain.oamLoadDisabled;
    gMain.oamLoadDisabled = TRUE;
    AddSpritesToOamBuffer();
    CopyMatricesToOamBuffer();
    gMain.oamLoadDisabled = temp;
    gShouldProcessSpriteCopyRequests = TRUE;
}

void UpdateOamCoords(void)
{
    u8 i;
    for (i = 0; i < MAX_SPRITES; i++)
    {
        struct Sprite *sprite = &gSprites[i];
        if (sprite->inUse && !sprite->invisible)
        {
            if (sprite->coordOffsetEnabled)
            {
                sprite->oam.x = sprite->pos1.x + sprite->pos2.x + sprite->centerToCornerVecX + gSpriteCoordOffsetX;
                sprite->oam.y = sprite->pos1.y + sprite->pos2.y + sprite->centerToCornerVecY + gSpriteCoordOffsetY;
            }
            else
            {
                sprite->oam.x = sprite->pos1.x + sprite->pos2.x + sprite->centerToCornerVecX;
                sprite->oam.y = sprite->pos1.y + sprite->pos2.y + sprite->centerToCornerVecY;
            }
        }
    }
}

void BuildSpritePriorities(void)
{
    u16 i;
    for (i = 0; i < MAX_SPRITES; i++)
    {
        struct Sprite *sprite = &gSprites[i];
        u16 priority = sprite->subpriority | (sprite->oam.priority << 8);
        gSpritePriorities[i] = priority;
    }
}

void SortSprites(void)
{
    u8 i;
    for (i = 1; i < MAX_SPRITES; i++)
    {
        u8 j = i;
        struct Sprite *sprite1 = &gSprites[gSpriteOrder[i - 1]];
        struct Sprite *sprite2 = &gSprites[gSpriteOrder[i]];
        u16 sprite1Priority = gSpritePriorities[gSpriteOrder[i - 1]];
        u16 sprite2Priority = gSpritePriorities[gSpriteOrder[i]];
        s16 sprite1Y = sprite1->oam.y;
        s16 sprite2Y = sprite2->oam.y;

        if (sprite1Y >= DISPLAY_HEIGHT)
            sprite1Y = sprite1Y - 256;

        if (sprite2Y >= DISPLAY_HEIGHT)
            sprite2Y = sprite2Y - 256;

        if (sprite1->oam.affineMode == ST_OAM_AFFINE_DOUBLE
         && sprite1->oam.size == 3)
        {
            u32 shape = sprite1->oam.shape;
            if (shape == ST_OAM_SQUARE || shape == 2)
            {
                if (sprite1Y > 128)
                    sprite1Y = sprite1Y - 256;
            }
        }

        if (sprite2->oam.affineMode == ST_OAM_AFFINE_DOUBLE
         && sprite2->oam.size == 3)
        {
            u32 shape = sprite2->oam.shape;
            if (shape == ST_OAM_SQUARE || shape == ST_OAM_V_RECTANGLE)
            {
                if (sprite2Y > 128)
                    sprite2Y = sprite2Y - 256;
            }
        }

        while (j > 0
            && ((sprite1Priority > sprite2Priority)
             || (sprite1Priority == sprite2Priority && sprite1Y < sprite2Y)))
        {
            u8 temp = gSpriteOrder[j];
            gSpriteOrder[j] = gSpriteOrder[j - 1];
            gSpriteOrder[j - 1] = temp;

            // UB: If j equals 1, then j-- makes j equal 0.
            // Then, gSpriteOrder[-1] gets accessed below.
            // Although this doesn't result in a bug in the ROM,
            // the behavior is undefined.
            j--;

            sprite1 = &gSprites[gSpriteOrder[j - 1]];
            sprite2 = &gSprites[gSpriteOrder[j]];
            sprite1Priority = gSpritePriorities[gSpriteOrder[j - 1]];
            sprite2Priority = gSpritePriorities[gSpriteOrder[j]];
            sprite1Y = sprite1->oam.y;
            sprite2Y = sprite2->oam.y;

            if (sprite1Y >= DISPLAY_HEIGHT)
                sprite1Y = sprite1Y - 256;

            if (sprite2Y >= DISPLAY_HEIGHT)
                sprite2Y = sprite2Y - 256;

            if (sprite1->oam.affineMode == ST_OAM_AFFINE_DOUBLE
             && sprite1->oam.size == 3)
            {
                u32 shape = sprite1->oam.shape;
                if (shape == ST_OAM_SQUARE || shape == ST_OAM_V_RECTANGLE)
                {
                    if (sprite1Y > 128)
                        sprite1Y = sprite1Y - 256;
                }
            }

            if (sprite2->oam.affineMode == ST_OAM_AFFINE_DOUBLE
             && sprite2->oam.size == 3)
            {
                u32 shape = sprite2->oam.shape;
                if (shape == ST_OAM_SQUARE || shape == ST_OAM_V_RECTANGLE)
                {
                    if (sprite2Y > 128)
                        sprite2Y = sprite2Y - 256;
                }
            }
        }
    }
}

void CopyMatricesToOamBuffer(void)
{
    u8 i;
    for (i = 0; i < OAM_MATRIX_COUNT; i++)
    {
        u32 base = 4 * i;
        gMain.oamBuffer[base + 0].affineParam = gOamMatrices[i].a;
        gMain.oamBuffer[base + 1].affineParam = gOamMatrices[i].b;
        gMain.oamBuffer[base + 2].affineParam = gOamMatrices[i].c;
        gMain.oamBuffer[base + 3].affineParam = gOamMatrices[i].d;
    }
}

void AddSpritesToOamBuffer(void)
{
    u8 i = 0;
    u8 oamIndex = 0;

    while (i < MAX_SPRITES)
    {
        struct Sprite *sprite = &gSprites[gSpriteOrder[i]];
        if (sprite->inUse && !sprite->invisible && AddSpriteToOamBuffer(sprite, &oamIndex))
            return;
        i++;
    }

    while (oamIndex < gOamLimit)
    {
        gMain.oamBuffer[oamIndex] = gDummyOamData;
        oamIndex++;
    }
}

u8 CreateSprite(const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
        if (!gSprites[i].inUse)
            return CreateSpriteAt(i, template, x, y, subpriority);

    return MAX_SPRITES;
}

u8 CreateSpriteAtEnd(const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    s16 i;

    for (i = MAX_SPRITES - 1; i > -1; i--)
        if (!gSprites[i].inUse)
            return CreateSpriteAt(i, template, x, y, subpriority);

    return MAX_SPRITES;
}

u8 CreateInvisibleSprite(void (*callback)(struct Sprite *))
{
    u8 index = CreateSprite(&gDummySpriteTemplate, 0, 0, 31);

    if (index == MAX_SPRITES)
    {
        return MAX_SPRITES;
    }
    else
    {
        gSprites[index].invisible = TRUE;
        gSprites[index].callback = callback;
        return index;
    }
}

u8 CreateSpriteAt(u8 index, const struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    struct Sprite *sprite = &gSprites[index];

    ResetSprite(sprite);

    sprite->inUse = TRUE;
    sprite->animBeginning = TRUE;
    sprite->affineAnimBeginning = TRUE;
    sprite->usingSheet = TRUE;

    sprite->subpriority = subpriority;
    sprite->oam = *template->oam;
    sprite->anims = template->anims;
    sprite->affineAnims = template->affineAnims;
    sprite->template = template;
    sprite->callback = template->callback;
    sprite->pos1.x = x;
    sprite->pos1.y = y;

    CalcCenterToCornerVec(sprite, sprite->oam.shape, sprite->oam.size, sprite->oam.affineMode);

    if (template->tileTag == 0xFFFF)
    {
        s16 tileNum;
        sprite->images = template->images;
        tileNum = AllocSpriteTiles((u8)(sprite->images->size / TILE_SIZE_4BPP));
        if (tileNum == -1)
        {
            ResetSprite(sprite);
            return MAX_SPRITES;
        }
        sprite->oam.tileNum = tileNum;
        sprite->usingSheet = FALSE;
        sprite->sheetTileStart = 0;
    }
    else
    {
        sprite->sheetTileStart = GetSpriteTileStartByTag(template->tileTag);
        SetSpriteSheetFrameTileNum(sprite);
    }

    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
        InitSpriteAffineAnim(sprite);

    if (template->paletteTag != 0xFFFF)
        sprite->oam.paletteNum = IndexOfSpritePaletteTag(template->paletteTag);

    return index;
}

u8 CreateSpriteAndAnimate(struct SpriteTemplate *template, s16 x, s16 y, u8 subpriority)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
    {
        struct Sprite *sprite = &gSprites[i];

        if (!gSprites[i].inUse)
        {
            u8 index = CreateSpriteAt(i, template, x, y, subpriority);

            if (index == MAX_SPRITES)
                return MAX_SPRITES;

            gSprites[i].callback(sprite);

            if (gSprites[i].inUse)
                AnimateSprite(sprite);

            return index;
        }
    }

    return MAX_SPRITES;
}

void DestroySprite(struct Sprite *sprite)
{
    if (sprite->inUse)
    {
        if (!sprite->usingSheet)
        {
            u16 i;
            u16 tileEnd = (sprite->images->size / TILE_SIZE_4BPP) + sprite->oam.tileNum;
            for (i = sprite->oam.tileNum; i < tileEnd; i++)
                FREE_SPRITE_TILE(i);
        }
        ResetSprite(sprite);
    }
}

void ResetOamRange(u8 a, u8 b)
{
    u8 i;

    for (i = a; i < b; i++)
    {
        struct OamData *oamBuffer = gMain.oamBuffer;
        oamBuffer[i] = *(struct OamData *)&gDummyOamData;
    }
}

void LoadOam(void)
{
    if (!gMain.oamLoadDisabled)
        CpuCopy32(gMain.oamBuffer, (void *)OAM, sizeof(gMain.oamBuffer));
}

void ClearSpriteCopyRequests(void)
{
    u8 i;

    gShouldProcessSpriteCopyRequests = FALSE;
    gSpriteCopyRequestCount = 0;

    for (i = 0; i < MAX_SPRITE_COPY_REQUESTS; i++)
    {
        gSpriteCopyRequests[i].src = 0;
        gSpriteCopyRequests[i].dest = 0;
        gSpriteCopyRequests[i].size = 0;
    }
}

void ResetOamMatrices(void)
{
    u8 i;
    for (i = 0; i < OAM_MATRIX_COUNT; i++)
    {
        // set to identity matrix
        gOamMatrices[i].a = 0x0100;
        gOamMatrices[i].b = 0x0000;
        gOamMatrices[i].c = 0x0000;
        gOamMatrices[i].d = 0x0100;
    }
}

void SetOamMatrix(u8 matrixNum, u16 a, u16 b, u16 c, u16 d)
{
    gOamMatrices[matrixNum].a = a;
    gOamMatrices[matrixNum].b = b;
    gOamMatrices[matrixNum].c = c;
    gOamMatrices[matrixNum].d = d;
}

void ResetSprite(struct Sprite *sprite)
{
    *sprite = sDummySprite;
}

void CalcCenterToCornerVec(struct Sprite *sprite, u8 shape, u8 size, u8 affineMode)
{
    u8 x = sCenterToCornerVecTable[shape][size][0];
    u8 y = sCenterToCornerVecTable[shape][size][1];

    if (affineMode & ST_OAM_AFFINE_DOUBLE_MASK)
    {
        x *= 2;
        y *= 2;
    }

    sprite->centerToCornerVecX = x;
    sprite->centerToCornerVecY = y;
}

s16 AllocSpriteTiles(u16 tileCount)
{
    u16 i;
    s16 start;
    u16 numTilesFound;

    if (tileCount == 0)
    {
        // Free all unreserved tiles if the tile count is 0.
        for (i = gReservedSpriteTileCount; i < TOTAL_OBJ_TILE_COUNT; i++)
            FREE_SPRITE_TILE(i);

        return 0;
    }

    i = gReservedSpriteTileCount;

    for (;;)
    {
        while (SPRITE_TILE_IS_ALLOCATED(i))
        {
            i++;

            if (i == TOTAL_OBJ_TILE_COUNT)
                return -1;
        }

        start = i;
        numTilesFound = 1;

        while (numTilesFound != tileCount)
        {
            i++;

            if (i == TOTAL_OBJ_TILE_COUNT)
                return -1;

            if (!SPRITE_TILE_IS_ALLOCATED(i))
                numTilesFound++;
            else
                break;
        }

        if (numTilesFound == tileCount)
            break;
    }

    for (i = start; i < tileCount + start; i++)
        ALLOC_SPRITE_TILE(i);

    return start;
}

u8 SpriteTileAllocBitmapOp(u16 bit, u8 op)
{
    u8 index = bit / 8;
    u8 shift = bit % 8;
    u8 val = bit % 8;
    u8 retVal = 0;

    if (op == 0)
    {
        val = ~(1 << val);
        gSpriteTileAllocBitmap[index] &= val;
    }
    else if (op == 1)
    {
        val = (1 << val);
        gSpriteTileAllocBitmap[index] |= val;
    }
    else
    {
        retVal = 1 << shift;
        retVal &= gSpriteTileAllocBitmap[index];
    }

    return retVal;
}

void SpriteCallbackDummy(struct Sprite *sprite)
{
}

void ProcessSpriteCopyRequests(void)
{
    if (gShouldProcessSpriteCopyRequests)
    {
        u8 i = 0;

        while (gSpriteCopyRequestCount > 0)
        {
            CpuCopy16(gSpriteCopyRequests[i].src, gSpriteCopyRequests[i].dest, gSpriteCopyRequests[i].size);
            gSpriteCopyRequestCount--;
            i++;
        }

        gShouldProcessSpriteCopyRequests = FALSE;
    }
}

void RequestSpriteFrameImageCopy(u16 index, u16 tileNum, const struct SpriteFrameImage *images)
{
    if (gSpriteCopyRequestCount < MAX_SPRITE_COPY_REQUESTS)
    {
        gSpriteCopyRequests[gSpriteCopyRequestCount].src = images[index].data;
        gSpriteCopyRequests[gSpriteCopyRequestCount].dest = (u8 *)OBJ_VRAM0 + TILE_SIZE_4BPP * tileNum;
        gSpriteCopyRequests[gSpriteCopyRequestCount].size = images[index].size;
        gSpriteCopyRequestCount++;
    }
}

void RequestSpriteCopy(const u8 *src, u8 *dest, u16 size)
{
    if (gSpriteCopyRequestCount < MAX_SPRITE_COPY_REQUESTS)
    {
        gSpriteCopyRequests[gSpriteCopyRequestCount].src = src;
        gSpriteCopyRequests[gSpriteCopyRequestCount].dest = dest;
        gSpriteCopyRequests[gSpriteCopyRequestCount].size = size;
        gSpriteCopyRequestCount++;
    }
}

void CopyFromSprites(u8 *dest)
{
    u32 i;
    u8 *src = (u8 *)gSprites;
    for (i = 0; i < sizeof(struct Sprite) * MAX_SPRITES; i++)
    {
        *dest = *src;
        dest++;
        src++;
    }
}

void CopyToSprites(u8 *src)
{
    u32 i;
    u8 *dest = (u8 *)gSprites;
    for (i = 0; i < sizeof(struct Sprite) * MAX_SPRITES; i++)
    {
        *dest = *src;
        src++;
        dest++;
    }
}

void ResetAllSprites(void)
{
    u8 i;

    for (i = 0; i < MAX_SPRITES; i++)
    {
        ResetSprite(&gSprites[i]);
        gSpriteOrder[i] = i;
    }

    ResetSprite(&gSprites[i]);
}

void FreeSpriteTiles(struct Sprite *sprite)
{
    if (sprite->template->tileTag != 0xFFFF)
        FreeSpriteTilesByTag(sprite->template->tileTag);
}

void FreeSpritePalette(struct Sprite *sprite)
{
    FreeSpritePaletteByTag(sprite->template->paletteTag);
}

void FreeSpriteOamMatrix(struct Sprite *sprite)
{
    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
    {
        FreeOamMatrix(sprite->oam.matrixNum);
        sprite->oam.affineMode = ST_OAM_AFFINE_OFF;
    }
}

void DestroySpriteAndFreeResources(struct Sprite *sprite)
{
    FreeSpriteTiles(sprite);
    FreeSpritePalette(sprite);
    FreeSpriteOamMatrix(sprite);
    DestroySprite(sprite);
}

void AnimateSprite(struct Sprite *sprite)
{
    sAnimFuncs[sprite->animBeginning](sprite);

    if (!gAffineAnimsDisabled)
        sAffineAnimFuncs[sprite->affineAnimBeginning](sprite);
}

void BeginAnim(struct Sprite *sprite)
{
    s16 imageValue;
    u8 duration;
    u8 hFlip;
    u8 vFlip;

    sprite->animCmdIndex = 0;
    sprite->animEnded = FALSE;
    sprite->animLoopCounter = 0;
    imageValue = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.imageValue;

    if (imageValue != -1)
    {
        sprite->animBeginning = FALSE;
        duration = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.duration;
        hFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.hFlip;
        vFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.vFlip;

        if (duration)
            duration--;

        sprite->animDelayCounter = duration;

        if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
            SetSpriteOamFlipBits(sprite, hFlip, vFlip);

        if (sprite->usingSheet)
            sprite->oam.tileNum = sprite->sheetTileStart + imageValue;
        else
            RequestSpriteFrameImageCopy(imageValue, sprite->oam.tileNum, sprite->images);
    }
}

void ContinueAnim(struct Sprite *sprite)
{
    if (sprite->animDelayCounter)
    {
        u8 hFlip;
        u8 vFlip;
        DecrementAnimDelayCounter(sprite);
        hFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.hFlip;
        vFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.vFlip;
        if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
            SetSpriteOamFlipBits(sprite, hFlip, vFlip);
    }
    else if (!sprite->animPaused)
    {
        s16 type;
        s16 funcIndex;
        sprite->animCmdIndex++;
        type = sprite->anims[sprite->animNum][sprite->animCmdIndex].type;
        funcIndex = 3;
        if (type < 0)
            funcIndex = type + 3;
        sAnimCmdFuncs[funcIndex](sprite);
    }
}

void AnimCmd_frame(struct Sprite *sprite)
{
    s16 imageValue;
    u8 duration;
    u8 hFlip;
    u8 vFlip;

    imageValue = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.imageValue;
    duration = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.duration;
    hFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.hFlip;
    vFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.vFlip;

    if (duration)
        duration--;

    sprite->animDelayCounter = duration;

    if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
        SetSpriteOamFlipBits(sprite, hFlip, vFlip);

    if (sprite->usingSheet)
        sprite->oam.tileNum = sprite->sheetTileStart + imageValue;
    else
        RequestSpriteFrameImageCopy(imageValue, sprite->oam.tileNum, sprite->images);
}

void AnimCmd_end(struct Sprite *sprite)
{
    sprite->animCmdIndex--;
    sprite->animEnded = TRUE;
}

void AnimCmd_jump(struct Sprite *sprite)
{
    s16 imageValue;
    u8 duration;
    u8 hFlip;
    u8 vFlip;

    sprite->animCmdIndex = sprite->anims[sprite->animNum][sprite->animCmdIndex].jump.target;

    imageValue = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.imageValue;
    duration = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.duration;
    hFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.hFlip;
    vFlip = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.vFlip;

    if (duration)
        duration--;

    sprite->animDelayCounter = duration;

    if (!(sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK))
        SetSpriteOamFlipBits(sprite, hFlip, vFlip);

    if (sprite->usingSheet)
        sprite->oam.tileNum = sprite->sheetTileStart + imageValue;
    else
        RequestSpriteFrameImageCopy(imageValue, sprite->oam.tileNum, sprite->images);
}

void AnimCmd_loop(struct Sprite *sprite)
{
    if (sprite->animLoopCounter)
        ContinueAnimLoop(sprite);
    else
        BeginAnimLoop(sprite);
}

void BeginAnimLoop(struct Sprite *sprite)
{
    sprite->animLoopCounter = sprite->anims[sprite->animNum][sprite->animCmdIndex].loop.count;
    JumpToTopOfAnimLoop(sprite);
    ContinueAnim(sprite);
}

void ContinueAnimLoop(struct Sprite *sprite)
{
    sprite->animLoopCounter--;
    JumpToTopOfAnimLoop(sprite);
    ContinueAnim(sprite);
}

void JumpToTopOfAnimLoop(struct Sprite *sprite)
{
    if (sprite->animLoopCounter)
    {
        sprite->animCmdIndex--;

        while (sprite->anims[sprite->animNum][sprite->animCmdIndex - 1].type != -3)
        {
            if (sprite->animCmdIndex == 0)
                break;
            sprite->animCmdIndex--;
        }

        sprite->animCmdIndex--;
    }
}

void BeginAffineAnim(struct Sprite *sprite)
{
    if ((sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK) && sprite->affineAnims[0][0].type != 32767)
    {
        struct AffineAnimFrameCmd frameCmd;
        u8 matrixNum = GetSpriteMatrixNum(sprite);
        AffineAnimStateRestartAnim(matrixNum);
        GetAffineAnimFrame(matrixNum, sprite, &frameCmd);
        sprite->affineAnimBeginning = FALSE;
        sprite->affineAnimEnded = FALSE;
        ApplyAffineAnimFrame(matrixNum, &frameCmd);
        sAffineAnimStates[matrixNum].delayCounter = frameCmd.duration;
        if (sprite->flags_f)
            obj_update_pos2(sprite, sprite->data6, sprite->data7);
    }
}

void ContinueAffineAnim(struct Sprite *sprite)
{
    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
    {
        u8 matrixNum = GetSpriteMatrixNum(sprite);

        if (sAffineAnimStates[matrixNum].delayCounter)
            AffineAnimDelay(matrixNum, sprite);
        else if (sprite->affineAnimPaused)
            return;
        else
        {
            s16 type;
            s16 funcIndex;
            sAffineAnimStates[matrixNum].animCmdIndex++;
            type = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].type;
            funcIndex = 3;
            if (type >= 32765)
                funcIndex = type - 32765;
            sAffineAnimCmdFuncs[funcIndex](matrixNum, sprite);
        }
        if (sprite->flags_f)
            obj_update_pos2(sprite, sprite->data6, sprite->data7);
    }
}

void AffineAnimDelay(u8 matrixNum, struct Sprite *sprite)
{
    if (!DecrementAffineAnimDelayCounter(sprite, matrixNum))
    {
        struct AffineAnimFrameCmd frameCmd;
        GetAffineAnimFrame(matrixNum, sprite, &frameCmd);
        ApplyAffineAnimFrameRelativeAndUpdateMatrix(matrixNum, &frameCmd);
    }
}

void AffineAnimCmd_loop(u8 matrixNum, struct Sprite *sprite)
{
    if (sAffineAnimStates[matrixNum].loopCounter)
        ContinueAffineAnimLoop(matrixNum, sprite);
    else
        BeginAffineAnimLoop(matrixNum, sprite);
}

void BeginAffineAnimLoop(u8 matrixNum, struct Sprite *sprite)
{
    sAffineAnimStates[matrixNum].loopCounter = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].loop.count;
    JumpToTopOfAffineAnimLoop(matrixNum, sprite);
    ContinueAffineAnim(sprite);
}

void ContinueAffineAnimLoop(u8 matrixNum, struct Sprite *sprite)
{
    sAffineAnimStates[matrixNum].loopCounter--;
    JumpToTopOfAffineAnimLoop(matrixNum, sprite);
    ContinueAffineAnim(sprite);
}

void JumpToTopOfAffineAnimLoop(u8 matrixNum, struct Sprite *sprite)
{
    if (sAffineAnimStates[matrixNum].loopCounter)
    {
        sAffineAnimStates[matrixNum].animCmdIndex--;

        while (sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex - 1].type != 32765)
        {
            if (sAffineAnimStates[matrixNum].animCmdIndex == 0)
                break;
            sAffineAnimStates[matrixNum].animCmdIndex--;
        }

        sAffineAnimStates[matrixNum].animCmdIndex--;
    }
}

void AffineAnimCmd_jump(u8 matrixNum, struct Sprite *sprite)
{
    struct AffineAnimFrameCmd frameCmd;
    sAffineAnimStates[matrixNum].animCmdIndex = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].jump.target;
    GetAffineAnimFrame(matrixNum, sprite, &frameCmd);
    ApplyAffineAnimFrame(matrixNum, &frameCmd);
    sAffineAnimStates[matrixNum].delayCounter = frameCmd.duration;
}

void AffineAnimCmd_end(u8 matrixNum, struct Sprite *sprite)
{
    struct AffineAnimFrameCmd dummyFrameCmd = {0};
    sprite->affineAnimEnded = TRUE;
    sAffineAnimStates[matrixNum].animCmdIndex--;
    ApplyAffineAnimFrameRelativeAndUpdateMatrix(matrixNum, &dummyFrameCmd);
}

void AffineAnimCmd_frame(u8 matrixNum, struct Sprite *sprite)
{
    struct AffineAnimFrameCmd frameCmd;
    GetAffineAnimFrame(matrixNum, sprite, &frameCmd);
    ApplyAffineAnimFrame(matrixNum, &frameCmd);
    sAffineAnimStates[matrixNum].delayCounter = frameCmd.duration;
}

void CopyOamMatrix(u8 destMatrixIndex, struct OamMatrix *srcMatrix)
{
    gOamMatrices[destMatrixIndex].a = srcMatrix->a;
    gOamMatrices[destMatrixIndex].b = srcMatrix->b;
    gOamMatrices[destMatrixIndex].c = srcMatrix->c;
    gOamMatrices[destMatrixIndex].d = srcMatrix->d;
}

u8 GetSpriteMatrixNum(struct Sprite *sprite)
{
    u8 matrixNum = 0;
    if (sprite->oam.affineMode & ST_OAM_AFFINE_ON_MASK)
        matrixNum = sprite->oam.matrixNum;
    return matrixNum;
}

void sub_8007E18(struct Sprite* sprite, s16 a2, s16 a3)
{
    sprite->data6 = a2;
    sprite->data7 = a3;
    sprite->flags_f = 1;
}

s32 sub_8007E28(s32 a0, s32 a1, s32 a2)
{
    s32 subResult, var1;

    subResult = a1 - a0;
    if (subResult < 0)
        var1 = -(subResult) >> 9;
    else
        var1 = -(subResult >> 9);
    return a2 - ((u32)(a2 * a1) / (u32)(a0) + var1);
}

#ifdef NONMATCHING
void obj_update_pos2(struct Sprite* sprite, s32 a1, s32 a2)
{
    s32 var0, var1, var2;
    u8 matrixNum = sprite->oam.matrixNum;
    if (a1 != 0x800)
    {
        var0 = gUnknown_082EC6F4[sprite->oam.size * 8 + sprite->oam.shape * 32];
        var1 = var0 << 8;
        var2 = (var0 << 16) / gOamMatrices[matrixNum].a;
        sprite->pos2.x = sub_8007E28(var1, var2, a1);
    }
    if (a2 != 0x800)
    {
        var0 = gUnknown_082EC6F4[4 + (sprite->oam.size * 8 + sprite->oam.shape * 32)];
        var1 = var0 << 8;
        var2 = (var0 << 16) / gOamMatrices[matrixNum].d;
        sprite->pos2.y = sub_8007E28(var1, var2, a2);
    }
}
#else
__attribute__((naked))
void obj_update_pos2(struct Sprite* sprite, s32 a1, s32 a2)
{
    asm(".syntax unified\n\
	push {r4-r7,lr}\n\
	mov r7, r9\n\
	mov r6, r8\n\
	push {r6,r7}\n\
	adds r5, r0, 0\n\
	adds r6, r1, 0\n\
	mov r8, r2\n\
	ldrb r1, [r5, 0x3]\n\
	lsls r0, r1, 26\n\
	lsrs r7, r0, 27\n\
	movs r0, 0x80\n\
	lsls r0, 4\n\
	mov r9, r0\n\
	cmp r6, r9\n\
	beq _08007EA2\n\
	ldr r2, =gUnknown_082EC6F4\n\
	lsrs r1, 6\n\
	lsls r1, 3\n\
	ldrb r0, [r5, 0x1]\n\
	lsrs r0, 6\n\
	lsls r0, 5\n\
	adds r1, r0\n\
	adds r1, r2\n\
	ldr r0, [r1]\n\
	lsls r4, r0, 8\n\
	lsls r0, 16\n\
	ldr r2, =gOamMatrices\n\
	lsls r1, r7, 3\n\
	adds r1, r2\n\
	movs r2, 0\n\
	ldrsh r1, [r1, r2]\n\
	bl __divsi3\n\
	adds r1, r0, 0\n\
	adds r0, r4, 0\n\
	adds r2, r6, 0\n\
	bl sub_8007E28\n\
	strh r0, [r5, 0x24]\n\
_08007EA2:\n\
	cmp r8, r9\n\
	beq _08007EDA\n\
	ldr r2, =gUnknown_082EC6F4\n\
	ldrb r1, [r5, 0x3]\n\
	lsrs r1, 6\n\
	lsls r1, 3\n\
	ldrb r0, [r5, 0x1]\n\
	lsrs r0, 6\n\
	lsls r0, 5\n\
	adds r1, r0\n\
	adds r2, 0x4\n\
	adds r1, r2\n\
	ldr r0, [r1]\n\
	lsls r4, r0, 8\n\
	lsls r0, 16\n\
	ldr r2, =gOamMatrices\n\
	lsls r1, r7, 3\n\
	adds r1, r2\n\
	movs r2, 0x6\n\
	ldrsh r1, [r1, r2]\n\
	bl __divsi3\n\
	adds r1, r0, 0\n\
	adds r0, r4, 0\n\
	mov r2, r8\n\
	bl sub_8007E28\n\
	strh r0, [r5, 0x26]\n\
_08007EDA:\n\
	pop {r3,r4}\n\
	mov r8, r3\n\
	mov r9, r4\n\
	pop {r4-r7}\n\
	pop {r0}\n\
	bx r0\n\
	.pool\n\
        .syntax divided");
}
#endif // NONMATCHING

void SetSpriteOamFlipBits(struct Sprite *sprite, u8 hFlip, u8 vFlip)
{
    sprite->oam.matrixNum &= 0x7;
    sprite->oam.matrixNum |= (((hFlip ^ sprite->hFlip) & 1) << 3);
    sprite->oam.matrixNum |= (((vFlip ^ sprite->vFlip) & 1) << 4);
}

void AffineAnimStateRestartAnim(u8 matrixNum)
{
    sAffineAnimStates[matrixNum].animCmdIndex = 0;
    sAffineAnimStates[matrixNum].delayCounter = 0;
    sAffineAnimStates[matrixNum].loopCounter = 0;
}

void AffineAnimStateStartAnim(u8 matrixNum, u8 animNum)
{
    sAffineAnimStates[matrixNum].animNum = animNum;
    sAffineAnimStates[matrixNum].animCmdIndex = 0;
    sAffineAnimStates[matrixNum].delayCounter = 0;
    sAffineAnimStates[matrixNum].loopCounter = 0;
    sAffineAnimStates[matrixNum].xScale = 0x0100;
    sAffineAnimStates[matrixNum].yScale = 0x0100;
    sAffineAnimStates[matrixNum].rotation = 0;
}

void AffineAnimStateReset(u8 matrixNum)
{
    sAffineAnimStates[matrixNum].animNum = 0;
    sAffineAnimStates[matrixNum].animCmdIndex = 0;
    sAffineAnimStates[matrixNum].delayCounter = 0;
    sAffineAnimStates[matrixNum].loopCounter = 0;
    sAffineAnimStates[matrixNum].xScale = 0x0100;
    sAffineAnimStates[matrixNum].yScale = 0x0100;
    sAffineAnimStates[matrixNum].rotation = 0;
}

void ApplyAffineAnimFrameAbsolute(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd)
{
    sAffineAnimStates[matrixNum].xScale = frameCmd->xScale;
    sAffineAnimStates[matrixNum].yScale = frameCmd->yScale;
    sAffineAnimStates[matrixNum].rotation = frameCmd->rotation << 8;
}

void DecrementAnimDelayCounter(struct Sprite *sprite)
{
    if (!sprite->animPaused)
        sprite->animDelayCounter--;
}

bool8 DecrementAffineAnimDelayCounter(struct Sprite *sprite, u8 matrixNum)
{
    if (!sprite->affineAnimPaused)
        --sAffineAnimStates[matrixNum].delayCounter;
    return sprite->affineAnimPaused;
}

void ApplyAffineAnimFrameRelativeAndUpdateMatrix(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd)
{
    struct ObjAffineSrcData srcData;
    struct OamMatrix matrix;
    sAffineAnimStates[matrixNum].xScale += frameCmd->xScale;
    sAffineAnimStates[matrixNum].yScale += frameCmd->yScale;
    sAffineAnimStates[matrixNum].rotation = (sAffineAnimStates[matrixNum].rotation + (frameCmd->rotation << 8)) & ~0xFF;
    srcData.xScale = ConvertScaleParam(sAffineAnimStates[matrixNum].xScale);
    srcData.yScale = ConvertScaleParam(sAffineAnimStates[matrixNum].yScale);
    srcData.rotation = sAffineAnimStates[matrixNum].rotation;
    ObjAffineSet(&srcData, &matrix, 1, 2);
    CopyOamMatrix(matrixNum, &matrix);
}

s16 ConvertScaleParam(s16 scale)
{
    s32 val = 0x10000;
    return val / scale;
}

void GetAffineAnimFrame(u8 matrixNum, struct Sprite *sprite, struct AffineAnimFrameCmd *frameCmd)
{
    frameCmd->xScale = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].frame.xScale;
    frameCmd->yScale = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].frame.yScale;
    frameCmd->rotation = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].frame.rotation;
    frameCmd->duration = sprite->affineAnims[sAffineAnimStates[matrixNum].animNum][sAffineAnimStates[matrixNum].animCmdIndex].frame.duration;
}

void ApplyAffineAnimFrame(u8 matrixNum, struct AffineAnimFrameCmd *frameCmd)
{
    struct AffineAnimFrameCmd dummyFrameCmd = {0};

    if (frameCmd->duration)
    {
        frameCmd->duration--;
        ApplyAffineAnimFrameRelativeAndUpdateMatrix(matrixNum, frameCmd);
    }
    else
    {
        ApplyAffineAnimFrameAbsolute(matrixNum, frameCmd);
        ApplyAffineAnimFrameRelativeAndUpdateMatrix(matrixNum, &dummyFrameCmd);
    }
}

void StartSpriteAnim(struct Sprite *sprite, u8 animNum)
{
    sprite->animNum = animNum;
    sprite->animBeginning = TRUE;
    sprite->animEnded = FALSE;
}

void StartSpriteAnimIfDifferent(struct Sprite *sprite, u8 animNum)
{
    if (sprite->animNum != animNum)
        StartSpriteAnim(sprite, animNum);
}

void SeekSpriteAnim(struct Sprite *sprite, u8 animCmdIndex)
{
    u8 temp = sprite->animPaused;
    sprite->animCmdIndex = animCmdIndex - 1;
    sprite->animDelayCounter = 0;
    sprite->animBeginning = FALSE;
    sprite->animEnded = FALSE;
    sprite->animPaused = FALSE;
    ContinueAnim(sprite);
    if (sprite->animDelayCounter)
        sprite->animDelayCounter++;
    sprite->animPaused = temp;
}

void StartSpriteAffineAnim(struct Sprite *sprite, u8 animNum)
{
    u8 matrixNum = GetSpriteMatrixNum(sprite);
    AffineAnimStateStartAnim(matrixNum, animNum);
    sprite->affineAnimBeginning = TRUE;
    sprite->affineAnimEnded = FALSE;
}

void StartSpriteAffineAnimIfDifferent(struct Sprite *sprite, u8 animNum)
{
    u8 matrixNum = GetSpriteMatrixNum(sprite);
    if (sAffineAnimStates[matrixNum].animNum != animNum)
        StartSpriteAffineAnim(sprite, animNum);
}

void ChangeSpriteAffineAnim(struct Sprite *sprite, u8 animNum)
{
    u8 matrixNum = GetSpriteMatrixNum(sprite);
    sAffineAnimStates[matrixNum].animNum = animNum;
    sprite->affineAnimBeginning = TRUE;
    sprite->affineAnimEnded = FALSE;
}

void ChangeSpriteAffineAnimIfDifferent(struct Sprite *sprite, u8 animNum)
{
    u8 matrixNum = GetSpriteMatrixNum(sprite);
    if (sAffineAnimStates[matrixNum].animNum != animNum)
        ChangeSpriteAffineAnim(sprite, animNum);
}

void SetSpriteSheetFrameTileNum(struct Sprite *sprite)
{
    if (sprite->usingSheet)
    {
        s16 tileOffset = sprite->anims[sprite->animNum][sprite->animCmdIndex].frame.imageValue;
        if (tileOffset < 0)
            tileOffset = 0;
        sprite->oam.tileNum = sprite->sheetTileStart + tileOffset;
    }
}

void ResetAffineAnimData(void)
{
    u8 i;

    gAffineAnimsDisabled = 0;
    gOamMatrixAllocBitmap = 0;

    ResetOamMatrices();

    for (i = 0; i < OAM_MATRIX_COUNT; i++)
        AffineAnimStateReset(i);
}

u8 AllocOamMatrix(void)
{
    u8 i = 0;
    u32 bit = 1;
    u32 bitmap = gOamMatrixAllocBitmap;

    while (i < OAM_MATRIX_COUNT)
    {
        if (!(bitmap & bit))
        {
            gOamMatrixAllocBitmap |= bit;
            return i;
        }

        i++;
        bit <<= 1;
    }

    return 0xFF;
}

void FreeOamMatrix(u8 matrixNum)
{
    u8 i = 0;
    u32 bit = 1;

    while (i < matrixNum)
    {
        i++;
        bit <<= 1;
    }

    gOamMatrixAllocBitmap &= ~bit;
    SetOamMatrix(matrixNum, 0x100, 0, 0, 0x100);
}

void InitSpriteAffineAnim(struct Sprite *sprite)
{
    u8 matrixNum = AllocOamMatrix();
    if (matrixNum != 0xFF)
    {
        CalcCenterToCornerVec(sprite, sprite->oam.shape, sprite->oam.size, sprite->oam.affineMode);
        sprite->oam.matrixNum = matrixNum;
        sprite->affineAnimBeginning = TRUE;
        AffineAnimStateReset(matrixNum);
    }
}

void SetOamMatrixRotationScaling(u8 matrixNum, s16 xScale, s16 yScale, u16 rotation)
{
    struct ObjAffineSrcData srcData;
    struct OamMatrix matrix;
    srcData.xScale = ConvertScaleParam(xScale);
    srcData.yScale = ConvertScaleParam(yScale);
    srcData.rotation = rotation;
    ObjAffineSet(&srcData, &matrix, 1, 2);
    CopyOamMatrix(matrixNum, &matrix);
}

u16 LoadSpriteSheet(const struct SpriteSheet *sheet)
{
    s16 tileStart = AllocSpriteTiles(sheet->size / TILE_SIZE_4BPP);

    if (tileStart < 0)
    {
        return 0;
    }
    else
    {
        AllocSpriteTileRange(sheet->tag, (u16)tileStart, sheet->size / TILE_SIZE_4BPP);
        CpuCopy16(sheet->data, (u8 *)OBJ_VRAM0 + TILE_SIZE_4BPP * tileStart, sheet->size);
        return (u16)tileStart;
    }
}

void LoadSpriteSheets(const struct SpriteSheet *sheets)
{
    u8 i;
    for (i = 0; sheets[i].data != NULL; i++)
        LoadSpriteSheet(&sheets[i]);
}
