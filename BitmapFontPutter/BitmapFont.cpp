﻿#include <Windows.h>
#include <stdint.h>
#include <algorithm>
#include <fstream>

#include <sys/types.h>
#include <cassert>

#include "CharCodeConverter.h"
#include "BitmapFont.h"
#include "Framework/Headers/Constants.h"

namespace
{

#pragma pack(1)
struct BitmapFileHeader
{
    uint8_t bfType[2];
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
};
#pragma pack()

#pragma pack(1)
struct BitmapInfoHeader
{
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPixPerMeter;
    int32_t biYPixPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};
#pragma pack()

uint8_t* GetBmpFileData(uint8_t* rawDataBuffer, uint32_t& bmpWidth, uint32_t& bmpHeight, uint32_t& bitCount)
{
    const BitmapFileHeader* bmp_file_header_ptr = (BitmapFileHeader*)(rawDataBuffer);
    const BitmapInfoHeader* bmp_info_header_ptr = (BitmapInfoHeader*)(rawDataBuffer + sizeof(BitmapFileHeader));
    bmpWidth = bmp_info_header_ptr->biWidth;
    bmpHeight = bmp_info_header_ptr->biHeight;
    bitCount = bmp_info_header_ptr->biBitCount;

    uint32_t test = sizeof(BitmapFileHeader);
    uint32_t a = sizeof(BitmapInfoHeader);
    uint8_t* bmpDataBuffer = rawDataBuffer + sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader);

    return bmpDataBuffer;
}

const uint32_t FONT_IMAGE_AREA_NUM = 94; // 1 区画あたりの文字数
const uint32_t FONT_IMAGE_LINE_NUM = 16; // 1 行の文字数
const uint32_t FONT_CHAR_DATA_SIZE = BitmapFont::FONT_CHAR_WIDTH * BitmapFont::FONT_BIT; // 一文字あたりの幅サイズ

/**
 * 指定された引数をマルチバイト文字として扱います。
 *
 */
const char* GetCharLong(const char* ptr, int32_t* pBuffer)
{
    *pBuffer = (0xFF00 & *ptr << 8);
    *pBuffer += (0x00FF & *(++ptr));
    return ptr;
}

/**
 * マルチバイト文字か判定します。
 *
 */
bool IsLeadByteSJIS(int32_t c) { return ((((unsigned)(c) ^ 0x20U) - 0xA1U) < (unsigned)(94 / 2)); }

}

BitmapFont::BitmapFont()
    : m_pBmpRawData(nullptr)
    , m_pBmpPixelBuffer(nullptr)
    , m_BmpWidth(0)
    , m_BmpHeight(0)
    , m_BmpCharLizeSize(0)
    , m_CharCodeConverter(nullptr)
    , m_Color(0)
    , m_hScreenBitmap(NULL)
    , m_hScreenDeviceContext(NULL)
{

}

BitmapFont::~BitmapFont()
{

}

bool BitmapFont::Create(HWND hWnd, const char* filename)
{
    // 文字コード変換を初期化
    m_CharCodeConverter = new CharCodeConverter();
    if (!m_CharCodeConverter->Initialize())
    {
        return false;
    }

    // フォント画像ファイルを開く
    std::ifstream ifs;
    ifs.open(filename, std::ios::binary);
    assert(ifs);
    if (!ifs)
    {
        return false;
    }

    // 終端までシーク -> 現在位置を取得 = ファイルサイズを取得
    ifs.seekg(0, std::ios::end);
    const size_t size = ifs.tellg();
    m_pBmpRawData = new uint8_t[size];

    ifs.seekg(0);
    ifs.read((char*)(m_pBmpRawData), size);
    ifs.close();

    // 使用できるビットマップ画像であるか確認する
    uint32_t bitCount = 0;
    m_pBmpPixelBuffer = ::GetBmpFileData(m_pBmpRawData, m_BmpWidth, m_BmpHeight, bitCount);
    assert(bitCount == 32);
    if (bitCount != 32)
    {
        return false;
    }

    // 画面サイズ分の互換ビットマップを作成
    HDC hdc = GetDC(hWnd);
    m_hScreenBitmap = CreateCompatibleBitmap(hdc, Framework::Constants::WIDTH, Framework::Constants::HEIGHT);
    m_hScreenDeviceContext = CreateCompatibleDC(hdc);

    SelectObject(m_hScreenDeviceContext, m_hScreenBitmap);
    SelectObject(m_hScreenDeviceContext, GetStockObject(NULL_PEN));

    PatBlt(m_hScreenDeviceContext, 0, 0, Framework::Constants::WIDTH, Framework::Constants::HEIGHT, WHITENESS);

    ReleaseDC(hWnd, hdc);

    // ビットマップ一行分のサイズを求める
    m_BmpCharLizeSize = (m_BmpWidth * BitmapFont::FONT_CHAR_HEIGHT * BitmapFont::FONT_BIT);

    return true;
}

void BitmapFont::Destroy()
{
    if (m_hScreenDeviceContext)
    {
        DeleteDC(m_hScreenDeviceContext);
        m_hScreenDeviceContext = NULL;
    }

    if (m_hScreenBitmap)
    {
        DeleteObject(m_hScreenBitmap);
        m_hScreenBitmap = NULL;
    }

    if (m_pBmpRawData)
    {
        delete[] m_pBmpRawData;
        m_pBmpRawData = nullptr;
    }

    if (m_CharCodeConverter != nullptr)
    {
        m_CharCodeConverter->Finalize();
        delete m_CharCodeConverter;
        m_CharCodeConverter = nullptr;
    }
}

uint32_t BitmapFont::DrawSJISChar(const int32_t x, const int32_t y, const int32_t c)
{
    // ビットマップデータにおける文字の位置を取得する
    const uint32_t charPos = GetCharPos(c);
    // 半角スペースや判別不明文字の場合はここに入る
    if (charPos == 0)
    {
        // 空白をもうけるために幅だけを返す
        return BitmapFont::FONT_CHAR_WIDTH / 4;
    }

    // 実際に描画する文字幅の最大・最小を取得する
    int32_t minWidth = 0;
    int32_t maxWidth = BitmapFont::FONT_CHAR_WIDTH;
    GetCharWidth(charPos, &minWidth, &maxWidth);

    uint8_t* fontData = m_pBmpPixelBuffer + charPos;
    for (int32_t py = 0; py < BitmapFont::FONT_CHAR_HEIGHT; ++py)
    {
        // 実際にピクセルが存在する範囲のみで描画を行う
        for (int32_t px = minWidth; px < maxWidth; ++px)
        {
            uint8_t* bmpDataPtr = fontData;
            // 上下反転
            bmpDataPtr += BitmapFont::FONT_BIT * (m_BmpWidth * (BitmapFont::FONT_CHAR_HEIGHT - py) + px);
            // ピクセルデータ取得（ARGBの順番を想定）
            uint8_t bitmapR = (*(bmpDataPtr + 1)), bitmapG = (*(bmpDataPtr + 2)), bitmapB = (*(bmpDataPtr + 3)), bitmapA = (*(bmpDataPtr + 0));
            if (bitmapA == 0) continue;

            // アルファを反映させるっぽい計算
            const uint32_t avg = (bitmapR + bitmapG + bitmapB) / 3;
            const uint32_t bitmapInvA = (255 - bitmapA);
            const uint32_t color = avg + bitmapInvA;
            const float fColor = 1.0f - static_cast<float>(color) / 255.0f;
            const uint8_t inputR = (m_Color >> 0 & 0xFF), inputG = (m_Color >> 8 & 0xFF), inputB = (m_Color >> 16 & 0xFF); // 標準の RGB マクロ用の展開
            const uint8_t r = static_cast<uint8_t>(inputR * fColor) + bitmapInvA,
                          g = static_cast<uint8_t>(inputG * fColor) + bitmapInvA,
                          b = static_cast<uint8_t>(inputB * fColor) + bitmapInvA;
            SetPixel(m_hScreenDeviceContext, x + px, y + py, RGB(r, g, b));
        }
    }
    return maxWidth;
}

void BitmapFont::DrawSJISString(const int32_t x, const int32_t y, const char* str)
{
    const char *ptr = str;
    int32_t posX = x, posY = y;
    int32_t c = 0;
    while (*ptr)
    {
        if (IsLeadByteSJIS(*ptr)) ptr = GetCharLong(ptr, &c);
        else                      c = *ptr;
        if (*ptr)
        {
            posX += DrawSJISChar(posX, posY, c);
            ++ptr;
        }
    }
}

uint32_t BitmapFont::DrawChar(const int32_t x, const int32_t y, const int32_t c)
{
    return DrawSJISChar(x, y, m_CharCodeConverter->UTF16BEtoSJIS(c));
}

void BitmapFont::DrawString(const int32_t x, const int32_t y, const wchar_t* str)
{
    const wchar_t *ptr = str;
    int32_t posX = x, posY = y;
    while (*ptr)
    {
        posX += DrawChar(posX, posY, (*ptr));
        ++ptr;
    }
}

void BitmapFont::SetColor(const uint8_t r, const uint8_t g, const uint8_t b)
{
    m_Color = RGB(r, g, b);
}

void BitmapFont::ClearBuffer(const uint8_t r, const uint8_t g, const uint8_t b)
{
    HBRUSH brush = CreateSolidBrush(RGB(r, g, b));
    SelectObject(m_hScreenDeviceContext, brush);
    PatBlt(m_hScreenDeviceContext, 0, 0, Framework::Constants::WIDTH, Framework::Constants::HEIGHT, PATCOPY);
    DeleteObject(brush);
}

void BitmapFont::PresentBuffer(const HDC hDC)
{
    BitBlt(hDC, 0, 0, Framework::Constants::WIDTH, Framework::Constants::HEIGHT, m_hScreenDeviceContext, 0, 0, SRCCOPY);
}

bool BitmapFont::GetCharWidth(const uint32_t charPos, int32_t *minWidth, int32_t *maxWidth)
{
    int32_t min = BitmapFont::FONT_CHAR_WIDTH, max = 0;

    uint8_t* fontData = m_pBmpPixelBuffer + charPos;
    for (int32_t py = 0; py < BitmapFont::FONT_CHAR_HEIGHT; ++py)
    {
        for (int32_t px = 0; px < BitmapFont::FONT_CHAR_WIDTH; ++px)
        {
            uint8_t* bmpDataPtr = fontData;
            // 上下反転
            bmpDataPtr += BitmapFont::FONT_BIT * (m_BmpWidth * (BitmapFont::FONT_CHAR_HEIGHT - py) + px);
            // ピクセルデータ取得（ARGBの順番を想定）
            uint8_t a = (*(bmpDataPtr + 0));
            if (a == 0) continue;
            min = std::min<int32_t>(px, min);
            max = std::max<int32_t>(px, max);
        }
    }

    if (min != BitmapFont::FONT_CHAR_WIDTH && max != 0)
    {
        *minWidth = min;
        *maxWidth = max + 1;
    }

    return true;
}

uint32_t BitmapFont::GetCharPos(const uint32_t c)
{
    const uint32_t HANKAKU_SPACE = 0x0020; 
    const uint32_t ZENKAKU_SPACE = 0x8140;
    const uint32_t LIST_SIZE = 5;
    // 区画ごとにずれるので、適当な位置の先頭と終端の文字コードを配列で持つ
    const uint32_t CODE_START_LIST[LIST_SIZE] = { 0x0020, 0x00A0, 0x8140, 0x8740, 0x8890 };
    const uint32_t CODE_LAST_LIST[LIST_SIZE] = { 0x00DF, 0x00DF, 0x84BF, 0x879F, 0x987F };
    // 上の配列で持つ文字コードは、画像データ中で下から何番目の位置にあるかの配列（画像によって要調整）
    const uint32_t ON_BITMAP_POS_LIST[LIST_SIZE] = { 328, 320, 316, 284, 254 };

    // 文字のデータ位置
    uint32_t charPos = 0;

    // 半角スペース or 全角スペース
    if (HANKAKU_SPACE == c || ZENKAKU_SPACE == c)
    {
        return 0;
    }
    else
    {
        // 位置ごとにテーブルから画像アドレスを計算する
        for (int32_t i = 0; i < LIST_SIZE; ++i)
        {
            if (c >= CODE_START_LIST[i] && c <= CODE_LAST_LIST[i])
            {
                const uint32_t FIRST_START_POS = (m_BmpCharLizeSize * ON_BITMAP_POS_LIST[i]) + 16;
                // 各文字の位置を計算
                uint32_t offset = (c - CODE_START_LIST[i]);
                uint32_t lines = offset / FONT_IMAGE_LINE_NUM; // 何列目？
                uint32_t number = offset % FONT_IMAGE_LINE_NUM; // 何番目？
                charPos = FIRST_START_POS - (m_BmpCharLizeSize * lines) - ((FONT_IMAGE_LINE_NUM - number) * FONT_CHAR_DATA_SIZE);
            }
        }
    }
    return charPos;
}
