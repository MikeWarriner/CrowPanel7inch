/*
 * eez-framework
 *
 * MIT License
 * Copyright 2024 Envox d.o.o.
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "eez-flow.h"
#if EEZ_FOR_LVGL_LZ4_OPTION
#include "eez-flow-lz4.h"
#endif
#if EEZ_FOR_LVGL_SHA256_OPTION
extern "C" {
#include "eez-flow-sha256.h"
}
#endif

// -----------------------------------------------------------------------------
// core/action.cpp
// -----------------------------------------------------------------------------
#if defined(EEZ_FOR_LVGL)
#endif
namespace eez {
#if EEZ_OPTION_GUI
namespace gui {
#endif
void executeActionFunction(int actionId) {
#if defined(EEZ_FOR_LVGL)
	eez::flow::executeLvglActionHook(actionId - 1);
#else
    g_actionExecFunctions[actionId]();
#endif
}
#if EEZ_OPTION_GUI
} 
#endif
} 
// -----------------------------------------------------------------------------
// core/alloc.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#if defined(EEZ_FOR_LVGL)
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#endif
namespace eez {
#if defined(EEZ_FOR_LVGL)
void initAllocHeap(uint8_t *heap, size_t heapSize) {
}
void *alloc(size_t size, uint32_t id) {
#if LVGL_VERSION_MAJOR >= 9
    return lv_malloc(size);
#else
    return lv_mem_alloc(size);
#endif
}
void free(void *ptr) {
#if LVGL_VERSION_MAJOR >= 9
    lv_free(ptr);
#else
    lv_mem_free(ptr);
#endif
}
template<typename T> void freeObject(T *ptr) {
	ptr->~T();
#if LVGL_VERSION_MAJOR >= 9
    lv_free(ptr);
#else
	lv_mem_free(ptr);
#endif
}
void getAllocInfo(uint32_t &free, uint32_t &alloc) {
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
	free = mon.free_size;
	alloc = mon.total_size - mon.free_size;
}
#elif 0 && defined(__EMSCRIPTEN__)
void initAllocHeap(uint8_t *heap, size_t heapSize) {
}
void *alloc(size_t size, uint32_t id) {
    return ::malloc(size);
}
void free(void *ptr) {
    ::free(ptr);
}
template<typename T> void freeObject(T *ptr) {
	ptr->~T();
	::free(ptr);
}
void getAllocInfo(uint32_t &free, uint32_t &alloc) {
	free = 0;
	alloc = 0;
}
#else
static const size_t ALIGNMENT = 64;
static const size_t MIN_BLOCK_SIZE = 8;
struct AllocBlock {
	AllocBlock *next;
	int free;
	size_t size;
	uint32_t id;
};
static uint8_t *g_heap;
#if defined(EEZ_PLATFORM_STM32)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wparentheses"
#endif
EEZ_MUTEX_DECLARE(alloc);
#if defined(EEZ_PLATFORM_STM32)
#pragma GCC diagnostic pop
#endif
void initAllocHeap(uint8_t *heap, size_t heapSize) {
    g_heap = heap;
	AllocBlock *first = (AllocBlock *)g_heap;
	first->next = 0;
	first->free = 1;
	first->size = heapSize - sizeof(AllocBlock);
	EEZ_MUTEX_CREATE(alloc);
}
void *alloc(size_t size, uint32_t id) {
	if (size == 0) {
		return nullptr;
	}
	if (EEZ_MUTEX_WAIT(alloc, osWaitForever)) {
		AllocBlock *firstBlock = (AllocBlock *)g_heap;
		AllocBlock *block = firstBlock;
		size = ((size + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;
		while (block) {
			if (block->free && block->size >= size) {
				break;
			}
			block = block->next;
		}
		if (!block) {
			EEZ_MUTEX_RELEASE(alloc);
			return nullptr;
		}
		int remainingSize = block->size - size - sizeof(AllocBlock);
		if (remainingSize >= (int)MIN_BLOCK_SIZE) {
			auto newBlock = (AllocBlock *)((uint8_t *)block + sizeof(AllocBlock) + size);
			newBlock->next = block->next;
			newBlock->free = 1;
			newBlock->size = remainingSize;
			block->next = newBlock;
			block->size = size;
		}
		block->free = 0;
		block->id = id;
		EEZ_MUTEX_RELEASE(alloc);
		return block + 1;
	}
	return nullptr;
}
void free(void *ptr) {
	if (ptr == 0) {
		return;
	}
	if (EEZ_MUTEX_WAIT(alloc, osWaitForever)) {
		AllocBlock *firstBlock = (AllocBlock *)g_heap;
		AllocBlock *prevBlock = nullptr;
		AllocBlock *block = firstBlock;
		while (block && block + 1 < ptr) {
			prevBlock = block;
			block = block->next;
		}
		if (!block || block + 1 != ptr || block->free) {
			assert(false);
			EEZ_MUTEX_RELEASE(alloc);
			return;
		}
		memset(ptr, 0xCC, block->size);
		auto nextBlock = block->next;
		if (nextBlock && nextBlock->free) {
			if (prevBlock && prevBlock->free) {
				prevBlock->next = nextBlock->next;
				prevBlock->size += sizeof(AllocBlock) + block->size + sizeof(AllocBlock) + nextBlock->size;
			} else {
				block->next = nextBlock->next;
				block->size += sizeof(AllocBlock) + nextBlock->size;
				block->free = 1;
			}
		} else if (prevBlock && prevBlock->free) {
			prevBlock->next = nextBlock;
			prevBlock->size += sizeof(AllocBlock) + block->size;
		} else {
			block->free = 1;
		}
		EEZ_MUTEX_RELEASE(alloc);
	}
}
template<typename T> void freeObject(T *ptr) {
	ptr->~T();
	free(ptr);
}
#if OPTION_SCPI
void dumpAlloc(scpi_t *context) {
	AllocBlock *first = (AllocBlock *)g_heap;
	AllocBlock *block = first;
	while (block) {
		char buffer[100];
		if (block->free) {
			snprintf(buffer, sizeof(buffer), "FREE: %d", (int)block->size);
		} else {
			snprintf(buffer, sizeof(buffer), "ALOC (0x%08x): %d", (unsigned int)block->id, (int)block->size);
		}
		SCPI_ResultText(context, buffer);
		block = block->next;
	}
}
#endif
void getAllocInfo(uint32_t &free, uint32_t &alloc) {
	free = 0;
	alloc = 0;
	if (EEZ_MUTEX_WAIT(alloc, osWaitForever)) {
		AllocBlock *first = (AllocBlock *)g_heap;
		AllocBlock *block = first;
		while (block) {
			if (block->free) {
				free += block->size;
			} else {
				alloc += block->size;
			}
			block = block->next;
		}
		EEZ_MUTEX_RELEASE(alloc);
	}
}
#endif
} 
// -----------------------------------------------------------------------------
// core/assets.cpp
// -----------------------------------------------------------------------------
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#if EEZ_FOR_LVGL_LZ4_OPTION
#endif
#if EEZ_OPTION_GUI
using namespace eez::gui;
#endif
#if OPTION_SCPI
#include <scpi/scpi.h>
#else
#define SCPI_ERROR_OUT_OF_DEVICE_MEMORY -321
#define SCPI_ERROR_INVALID_BLOCK_DATA -161
#endif
namespace eez {
bool g_isMainAssetsLoaded;
Assets *g_mainAssets;
bool g_mainAssetsUncompressed;
Assets *g_externalAssets;
void fixOffsets(Assets *assets);
bool decompressAssetsData(const uint8_t *assetsData, uint32_t assetsDataSize, Assets *decompressedAssets, uint32_t maxDecompressedAssetsSize, int *err) {
	uint32_t compressedDataOffset;
	uint32_t decompressedSize;
	auto header = (Header *)assetsData;
	if (header->tag == HEADER_TAG_COMPRESSED) {
		decompressedAssets->projectMajorVersion = header->projectMajorVersion;
		decompressedAssets->projectMinorVersion = header->projectMinorVersion;
        decompressedAssets->assetsType = header->assetsType;
		compressedDataOffset = sizeof(Header);
		decompressedSize = header->decompressedSize;
	} else {
		decompressedAssets->projectMajorVersion = PROJECT_VERSION_V2;
		decompressedAssets->projectMinorVersion = 0;
        decompressedAssets->assetsType = ASSETS_TYPE_RESOURCE;
		compressedDataOffset = 4;
		decompressedSize = header->tag;
	}
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
	auto decompressedDataOffset = offsetof(Assets, settings);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
	if (decompressedDataOffset + decompressedSize > maxDecompressedAssetsSize) {
		if (err) {
			*err = SCPI_ERROR_OUT_OF_DEVICE_MEMORY;
		}
		return false;
	}
	int compressedSize = assetsDataSize - compressedDataOffset;
#if EEZ_FOR_LVGL_LZ4_OPTION
    int decompressResult = LZ4_decompress_safe(
		(const char *)(assetsData + compressedDataOffset),
		(char *)decompressedAssets + decompressedDataOffset,
		compressedSize,
		decompressedSize
	);
	if (decompressResult != (int)decompressedSize) {
		if (err) {
			*err = SCPI_ERROR_INVALID_BLOCK_DATA;
		}
		return false;
	}
	return true;
#else
    *err = -1;
    return false;
#endif
}
void allocMemoryForDecompressedAssets(const uint8_t *assetsData, uint32_t assetsDataSize, uint8_t *&decompressedAssetsMemoryBuffer, uint32_t &decompressedAssetsMemoryBufferSize) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
	auto decompressedDataOffset = offsetof(Assets, settings);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    auto header = (Header *)assetsData;
    assert (header->tag == HEADER_TAG_COMPRESSED);
    uint32_t decompressedSize = header->decompressedSize;
    decompressedAssetsMemoryBufferSize = decompressedDataOffset + decompressedSize;
    decompressedAssetsMemoryBuffer = (uint8_t *)eez::alloc(decompressedAssetsMemoryBufferSize, 0x587da194);
}
void loadMainAssets(const uint8_t *assets, uint32_t assetsSize) {
    auto header = (Header *)assets;
    if (header->tag == HEADER_TAG) {
        g_mainAssets = (Assets *)(assets + sizeof(uint32_t));
        g_mainAssetsUncompressed = true;
    } else {
#if defined(EEZ_FOR_LVGL)
        uint8_t *DECOMPRESSED_ASSETS_START_ADDRESS = 0;
        uint32_t MAX_DECOMPRESSED_ASSETS_SIZE = 0;
        allocMemoryForDecompressedAssets(assets, assetsSize, DECOMPRESSED_ASSETS_START_ADDRESS, MAX_DECOMPRESSED_ASSETS_SIZE);
#endif
        g_mainAssets = (Assets *)DECOMPRESSED_ASSETS_START_ADDRESS;
        g_mainAssetsUncompressed = false;
        g_mainAssets->external = false;
        auto decompressedSize = decompressAssetsData(assets, assetsSize, g_mainAssets, MAX_DECOMPRESSED_ASSETS_SIZE, nullptr);
        assert(decompressedSize);
    }
    g_isMainAssetsLoaded = true;
}
void unloadExternalAssets() {
	if (g_externalAssets) {
#if EEZ_OPTION_GUI
		removeExternalPagesFromTheStack();
#endif
		free(g_externalAssets);
		g_externalAssets = nullptr;
	}
}
#if EEZ_OPTION_GUI
const gui::PageAsset* getPageAsset(int pageId) {
	if (pageId > 0) {
		return g_mainAssets->pages[pageId - 1];
	} else if (pageId < 0) {
		if (g_externalAssets == nullptr) {
			return nullptr;
		}
		return g_externalAssets->pages[-pageId - 1];
	}
	return nullptr;
}
const gui::PageAsset *getPageAsset(int pageId, WidgetCursor& widgetCursor) {
	if (pageId < 0) {
		widgetCursor.assets = g_externalAssets;
		widgetCursor.flowState = flow::getPageFlowState(g_externalAssets, -pageId - 1, widgetCursor);
	} else {
	    widgetCursor.assets = g_mainAssets;
		if (g_mainAssets->flowDefinition) {
			widgetCursor.flowState = flow::getPageFlowState(g_mainAssets, pageId - 1, widgetCursor);
		}
    }
	return getPageAsset(pageId);
}
const gui::Style *getStyle(int styleID) {
	if (styleID > 0) {
		return g_mainAssets->styles[styleID - 1];
	} else if (styleID < 0) {
		if (g_externalAssets == nullptr) {
			return getStyle(STYLE_ID_DEFAULT);
		}
		return g_externalAssets->styles[-styleID - 1];
	}
	return getStyle(STYLE_ID_DEFAULT);
}
const gui::FontData *getFontData(int fontID) {
	if (fontID > 0) {
		return g_mainAssets->fonts[fontID - 1];
	} else if (fontID < 0) {
		if (g_externalAssets == nullptr) {
			return nullptr;
		}
		return g_externalAssets->fonts[-fontID - 1];
	}
	return nullptr;
}
const gui::Bitmap *getBitmap(int bitmapID) {
	if (bitmapID > 0) {
		return g_mainAssets->bitmaps[bitmapID - 1];
	} else if (bitmapID < 0) {
		if (g_externalAssets == nullptr) {
			return nullptr;
		}
		return g_externalAssets->bitmaps[-bitmapID - 1];
	}
	return nullptr;
}
const int getBitmapIdByName(const char *bitmapName) {
    for (uint32_t i = 0; i < g_mainAssets->bitmaps.count; i++) {
		if (strcmp(g_mainAssets->bitmaps[i]->name, bitmapName) == 0) {
            return i + 1;
        }
	}
    return 0;
}
#endif 
int getThemesCount() {
	return (int)g_mainAssets->colorsDefinition->themes.count;
}
Theme *getTheme(int i) {
    if (i < 0 || i >= (int)g_mainAssets->colorsDefinition->themes.count) {
        return nullptr;
    }
    return g_mainAssets->colorsDefinition->themes[i];
}
const char *getThemeName(int i) {
    auto theme = getTheme(i);
    if (!theme) {
	    return "";
    }
    return static_cast<const char *>(theme->name);
}
const uint32_t getThemeColorsCount(int themeIndex) {
    auto theme = getTheme(themeIndex);
    if (!theme) {
	    return 0;
    }
	return theme->colors.count;
}
const uint16_t *getThemeColors(int themeIndex) {
    auto theme = getTheme(themeIndex);
    if (!theme) {
        static uint16_t *g_themeColors = { 0 };
	    return g_themeColors;
    }
	return static_cast<uint16_t *>(theme->colors.items);
}
const uint16_t *getColors() {
	return static_cast<uint16_t *>(g_mainAssets->colorsDefinition->colors.items);
}
int getExternalAssetsMainPageId() {
	return -1;
}
#if EEZ_OPTION_GUI
const char *getActionName(const WidgetCursor &widgetCursor, int16_t actionId) {
	if (actionId == 0) {
		return nullptr;
	}
	if (actionId < 0) {
		actionId = -actionId;
	}
	actionId--;
	if (!widgetCursor.assets) {
		return "";
	}
	return widgetCursor.assets->actionNames[actionId];
}
int16_t getDataIdFromName(const WidgetCursor &widgetCursor, const char *name) {
	if (!widgetCursor.assets) {
		return 0;
	}
	for (uint32_t i = 0; i < widgetCursor.assets->variableNames.count; i++) {
		if (strcmp(widgetCursor.assets->variableNames[i], name) == 0) {
			return -((int16_t)i + 1);
		}
	}
	return 0;
}
#endif 
} 
// -----------------------------------------------------------------------------
// core/debug.cpp
// -----------------------------------------------------------------------------
#ifdef DEBUG
#include <cstdio>
#include <stdarg.h>
#include <string.h>
namespace eez {
namespace debug {
void Trace(TraceType traceType, const char *format, ...) {
    va_list args;
    va_start(args, format);
    static const size_t BUFFER_SIZE = 256;
    char buffer[BUFFER_SIZE + 1];
	vsnprintf(buffer, BUFFER_SIZE, format, args);
	buffer[BUFFER_SIZE] = 0;
    va_end(args);
    if (traceType == TRACE_TYPE_DEBUG) {
        pushDebugTraceHook(buffer, strlen(buffer));
    } else if (traceType == TRACE_TYPE_INFO) {
        pushInfoTraceHook(buffer, strlen(buffer));
    } else {
        pushErrorTraceHook(buffer, strlen(buffer));
    }
}
} 
} 
extern "C" void debug_trace(const char *str, size_t len) {
    eez::debug::pushDebugTraceHook(str, len);
}
#endif 
// -----------------------------------------------------------------------------
// core/memory.cpp
// -----------------------------------------------------------------------------
#include <assert.h>
#if defined(EEZ_FOR_LVGL)
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#endif
namespace eez {
#if (defined(EEZ_PLATFORM_SIMULATOR) || defined(__EMSCRIPTEN__)) && !defined(EEZ_FOR_LVGL)
uint8_t g_memory[MEMORY_SIZE] = { 0 };
#endif
uint8_t *DECOMPRESSED_ASSETS_START_ADDRESS;
uint8_t *FLOW_TO_DEBUGGER_MESSAGE_BUFFER;
#if EEZ_OPTION_GUI
    uint8_t *VRAM_BUFFER1_START_ADDRESS;
    uint8_t *VRAM_BUFFER2_START_ADDRESS;
    #if EEZ_OPTION_GUI_ANIMATIONS
        uint8_t *VRAM_ANIMATION_BUFFER1_START_ADDRESS;
        uint8_t *VRAM_ANIMATION_BUFFER2_START_ADDRESS;
    #endif
    uint8_t *VRAM_AUX_BUFFER_START_ADDRESSES[NUM_AUX_BUFFERS];
    uint8_t *SCREENSHOOT_BUFFER_START_ADDRESS;
    uint8_t *GUI_STATE_BUFFER;
#endif
uint8_t *ALLOC_BUFFER = 0;
uint32_t ALLOC_BUFFER_SIZE = 0;
void initMemory() {
    initAssetsMemory();
    initOtherMemory();
}
void initAssetsMemory() {
#if defined(EEZ_FOR_LVGL)
#if defined(LV_MEM_SIZE)
    ALLOC_BUFFER_SIZE = LV_MEM_SIZE;
#endif
#else
    ALLOC_BUFFER = MEMORY_BEGIN;
    ALLOC_BUFFER_SIZE = MEMORY_SIZE;
    DECOMPRESSED_ASSETS_START_ADDRESS = allocBuffer(MAX_DECOMPRESSED_ASSETS_SIZE);
#endif
}
void initOtherMemory() {
#if !defined(EEZ_FOR_LVGL)
    FLOW_TO_DEBUGGER_MESSAGE_BUFFER = allocBuffer(FLOW_TO_DEBUGGER_MESSAGE_BUFFER_SIZE);
#endif
#if EEZ_OPTION_GUI
    uint32_t VRAM_BUFFER_SIZE = DISPLAY_WIDTH * DISPLAY_HEIGHT * DISPLAY_BPP / 8;
    VRAM_BUFFER1_START_ADDRESS = allocBuffer(VRAM_BUFFER_SIZE);
    VRAM_BUFFER2_START_ADDRESS = allocBuffer(VRAM_BUFFER_SIZE);
    for (size_t i = 0; i < NUM_AUX_BUFFERS; i++) {
        VRAM_AUX_BUFFER_START_ADDRESSES[i] = allocBuffer(VRAM_BUFFER_SIZE);
    }
#if EEZ_OPTION_GUI_ANIMATIONS
    VRAM_ANIMATION_BUFFER1_START_ADDRESS = allocBuffer(VRAM_BUFFER_SIZE);
    VRAM_ANIMATION_BUFFER2_START_ADDRESS = allocBuffer(VRAM_BUFFER_SIZE);
    SCREENSHOOT_BUFFER_START_ADDRESS = VRAM_ANIMATION_BUFFER1_START_ADDRESS;
#else
    SCREENSHOOT_BUFFER_START_ADDRESS = allocBuffer(VRAM_BUFFER_SIZE * 2);
#endif
    GUI_STATE_BUFFER = allocBuffer(GUI_STATE_BUFFER_SIZE);
#endif
}
uint8_t *allocBuffer(uint32_t size) {
#if defined(EEZ_FOR_LVGL)
#if LVGL_VERSION_MAJOR >= 9
    return (uint8_t *)lv_malloc(size);
#else
    return (uint8_t *)lv_mem_alloc(size);
#endif
#else
    size = ((size + 1023) / 1024) * 1024;
    auto buffer = ALLOC_BUFFER;
    assert(ALLOC_BUFFER_SIZE > size);
    ALLOC_BUFFER += size;
    ALLOC_BUFFER_SIZE -= size;
    return buffer;
#endif
}
} 
// -----------------------------------------------------------------------------
// core/os.cpp
// -----------------------------------------------------------------------------
#if defined(EEZ_PLATFORM_STM32)
#include <main.h>
#endif
#if defined(EEZ_PLATFORM_ESP32)
#include <esp_timer.h>
#endif
#if defined(EEZ_PLATFORM_PICO)
#include "pico/stdlib.h"
#endif
#if defined(EEZ_PLATFORM_RASPBERRY)
#include <circle/timer.h>
#endif
#if defined(EEZ_FOR_LVGL)
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#endif
#if defined(__EMSCRIPTEN__)
#include <sys/time.h>
#endif
namespace eez {
uint32_t millis() {
#if defined(EEZ_PLATFORM_STM32)
	return HAL_GetTick();
#elif defined(__EMSCRIPTEN__)
	return (uint32_t)emscripten_get_now();
#elif defined(EEZ_PLATFORM_SIMULATOR)
	return osKernelGetTickCount();
#elif defined(EEZ_PLATFORM_ESP32)
	return (unsigned long) (esp_timer_get_time() / 1000ULL);
#elif defined(EEZ_PLATFORM_PICO)
    auto abs_time = get_absolute_time();
    return to_ms_since_boot(abs_time);
#elif defined(EEZ_PLATFORM_RASPBERRY)
    unsigned nStartTicks = CTimer::Get()->GetClockTicks();
    return nStartTicks / 1000;
#elif defined(EEZ_FOR_LVGL)
    return lv_tick_get();
#else
    #error "Missing millis implementation";
#endif
}
} 
// -----------------------------------------------------------------------------
// core/unit.cpp
// -----------------------------------------------------------------------------
#include <string.h>
#if OPTION_SCPI
#include <scpi/types.h>
#endif
namespace eez {
const char *g_unitNames[] = {
    "", 
    "V", 
    "mV", 
    "A", 
    "mA", 
    "uA", 
    "W", 
    "mW", 
    "s", 
    "ms", 
    DEGREE_SYMBOL"C", 
    "rpm", 
    "\xb4", 
    "K\xb4", 
    "M\xb4", 
    "%", 
    "Hz", 
    "mHz", 
    "KHz", 
    "MHz", 
    "J", 
    "F", 
    "mF", 
    "uF", 
    "nF", 
    "pF", 
    "minutes", 
    "VA", 
    "VAR", 
	DEGREE_SYMBOL, 
	"Vpp", 
	"mVpp", 
	"App", 
	"mApp", 
	"uApp", 
};
const Unit g_baseUnit[] = {
	UNIT_NONE, 
	UNIT_VOLT, 
	UNIT_VOLT, 
	UNIT_AMPER, 
	UNIT_AMPER, 
	UNIT_AMPER, 
	UNIT_WATT, 
	UNIT_WATT, 
	UNIT_SECOND, 
	UNIT_SECOND, 
	UNIT_CELSIUS, 
	UNIT_RPM, 
	UNIT_OHM, 
	UNIT_OHM, 
	UNIT_OHM, 
	UNIT_PERCENT, 
	UNIT_HERTZ, 
	UNIT_HERTZ, 
	UNIT_HERTZ, 
	UNIT_HERTZ, 
	UNIT_JOULE, 
	UNIT_FARAD, 
	UNIT_FARAD, 
	UNIT_FARAD, 
	UNIT_FARAD, 
	UNIT_FARAD, 
	UNIT_SECOND, 
	UNIT_VOLT_AMPERE, 
	UNIT_VOLT_AMPERE, 
	UNIT_DEGREE, 
	UNIT_VOLT_PP, 
	UNIT_VOLT_PP, 
	UNIT_AMPER_PP, 
	UNIT_AMPER_PP, 
	UNIT_AMPER_PP, 
};
const float g_unitFactor[] = {
	1.0f, 
	1.0f, 
	1E-3f, 
	1.0f, 
	1E-3f, 
	1E-6f, 
	1.0f, 
	1E-3f, 
	1.0f, 
	1E-3f, 
	1.0f, 
	1.0f, 
	1.0f, 
	1E3f, 
	1E6f, 
	1.0f, 
	1.0f, 
	1E-3f, 
	1E3f, 
	1E6f, 
	1.0f, 
	1.0f, 
	1E-3f, 
	1E-6f, 
	1E-9f, 
	1E-12f, 
	60.0f, 
	1.0f, 
	1.0f, 
	1.0f, 
	1.0f, 
	1E-3f, 
	1.0f, 
	1E-3f, 
	1E-6f, 
};
#if OPTION_SCPI
static const int g_scpiUnits[] = {
    SCPI_UNIT_NONE, 
    SCPI_UNIT_VOLT, 
    SCPI_UNIT_VOLT, 
    SCPI_UNIT_AMPER, 
    SCPI_UNIT_AMPER, 
    SCPI_UNIT_AMPER, 
    SCPI_UNIT_WATT, 
    SCPI_UNIT_WATT, 
    SCPI_UNIT_SECOND, 
    SCPI_UNIT_SECOND, 
    SCPI_UNIT_CELSIUS, 
    SCPI_UNIT_NONE, 
    SCPI_UNIT_OHM, 
    SCPI_UNIT_OHM, 
    SCPI_UNIT_OHM, 
    SCPI_UNIT_NONE, 
    SCPI_UNIT_HERTZ, 
    SCPI_UNIT_HERTZ, 
    SCPI_UNIT_HERTZ, 
    SCPI_UNIT_HERTZ, 
    SCPI_UNIT_JOULE, 
    SCPI_UNIT_FARAD, 
    SCPI_UNIT_FARAD, 
    SCPI_UNIT_FARAD, 
    SCPI_UNIT_FARAD, 
    SCPI_UNIT_FARAD, 
	SCPI_UNIT_SECOND, 
	SCPI_UNIT_WATT, 
	SCPI_UNIT_WATT, 
	SCPI_UNIT_DEGREE, 
	SCPI_UNIT_VOLT, 
	SCPI_UNIT_VOLT, 
	SCPI_UNIT_AMPER, 
	SCPI_UNIT_AMPER, 
	SCPI_UNIT_AMPER, 
};
#endif
Unit getUnitFromName(const char *unitName) {
	if (unitName) {
		for (unsigned i = 0; i < sizeof(g_unitNames) / sizeof(const char *); i++) {
			if (strcmp(g_unitNames[i], unitName) == 0) {
				return (Unit)i;
			}
		}
	}
	return UNIT_NONE;
}
#if OPTION_SCPI
int getScpiUnit(Unit unit) {
    if (unit == UNIT_UNKNOWN) {
        return SCPI_UNIT_NONE;
    }
    return g_scpiUnits[unit];
}
#endif
Unit getBaseUnit(Unit unit) {
    if (unit == UNIT_UNKNOWN) {
        return UNIT_UNKNOWN;
    }
	return g_baseUnit[unit];
}
float getUnitFactor(Unit unit) {
    if (unit == UNIT_UNKNOWN) {
        return 1.0f;
    }
	return g_unitFactor[unit];
}
static Unit getDerivedUnit(Unit unit, float factor) {
	if (unit == UNIT_UNKNOWN) {
		return UNIT_UNKNOWN;
	}
	for (size_t i = 0; i < sizeof(g_baseUnit) / sizeof(Unit); i++) {
		if (g_baseUnit[i] == g_baseUnit[unit] && g_unitFactor[i] == factor) {
			return (Unit)i;
		}
	}
	return UNIT_UNKNOWN;
}
static const float FACTORS[] = { 1E-12F, 1E-9F, 1E-6F, 1E-3F, 1E0F, 1E3F, 1E6F, 1E9F, 1E12F };
Unit findDerivedUnit(float value, Unit unit) {
	Unit result;
	for (int factorIndex = 1; ; factorIndex++) {
		float factor = FACTORS[factorIndex];
		if (factor > 1.0F) {
			break;
		}
		if (value < factor) {
			result = getDerivedUnit(unit, FACTORS[factorIndex - 1]);
			if (result != UNIT_UNKNOWN) {
				return result;
			}
		}
	}
	for (int factorIndex = sizeof(FACTORS) / sizeof(float) - 1; factorIndex >= 0; factorIndex--) {
		float factor = FACTORS[factorIndex];
		if (factor == 1.0F) {
			break;
		}
		if (value >= factor) {
			result = getDerivedUnit(unit, factor);
			if (result != UNIT_UNKNOWN) {
				return result;
			}
		}
	}
	return unit;
}
float getSmallerFactor(float factor) {
	for (int factorIndex = sizeof(FACTORS) / sizeof(float) - 1; factorIndex > 0; factorIndex--) {
		float itFactor = FACTORS[factorIndex];
		if (itFactor < factor) {
			return itFactor;
		}
	}
	return FACTORS[0];
}
Unit getSmallerUnit(Unit unit, float min, float precision) {
	float factor = getUnitFactor(unit);
	if (precision <= factor || min <= factor) {
		return getDerivedUnit(unit, getSmallerFactor(factor));
	}
	return UNIT_UNKNOWN;
}
Unit getBiggestUnit(Unit unit, float max) {
	for (int factorIndex = sizeof(FACTORS) / sizeof(float) - 1; factorIndex >= 0; factorIndex--) {
		float factor = FACTORS[factorIndex];
		if (max >= factor) {
			auto result = getDerivedUnit(unit, factor);
			if (result != UNIT_UNKNOWN) {
				return result;
			}
		}
	}
	return UNIT_UNKNOWN;
}
Unit getSmallestUnit(Unit unit, float min, float precision) {
	for (int factorIndex = 0; factorIndex < int(sizeof(FACTORS) / sizeof(float)); factorIndex++) {
		float factor = FACTORS[factorIndex];
		if (precision <= factor || min <= factor) {
			auto result = getDerivedUnit(unit, factor);
			if (result != UNIT_UNKNOWN) {
				return result;
			}
		}
	}
	return UNIT_UNKNOWN;
}
} 
// -----------------------------------------------------------------------------
// core/util.cpp
// -----------------------------------------------------------------------------
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#if defined(EEZ_PLATFORM_STM32) && !defined(EEZ_FOR_LVGL)
#include <crc.h>
#endif
namespace eez {
float remap(float x, float x1, float y1, float x2, float y2) {
    return y1 + (x - x1) * (y2 - y1) / (x2 - x1);
}
float remapQuad(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t * t;
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}
float remapOutQuad(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t * (2 - t);
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}
float remapInOutQuad(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t < .5 ? 2 * t*t : -1 + (4 - 2 * t)*t;
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}
float remapCubic(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t * t * t;
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}
float remapOutCubic(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t - 1;
    t = 1 + t * t * t;
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}
float remapExp(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t == 0 ? 0 : float(pow(2, 10 * (t - 1)));
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}
float remapOutExp(float x, float x1, float y1, float x2, float y2) {
    float t = remap(x, x1, 0, x2, 1);
    t = t == 1 ? 1 : float(1 - pow(2, -10 * t));
    x = remap(t, 0, x1, 1, x2);
    return remap(x, x1, y1, x2, y2);
}
float clamp(float x, float min, float max) {
    if (x < min) {
        return min;
    }
    if (x > max) {
        return max;
    }
    return x;
}
void stringCopy(char *dst, size_t maxStrLength, const char *src) {
    strncpy(dst, src, maxStrLength);
    dst[maxStrLength - 1] = 0;
}
void stringCopyLength(char *dst, size_t maxStrLength, const char *src, size_t length) {
	size_t n = MIN(length, maxStrLength);
	strncpy(dst, src, n);
	dst[n] = 0;
}
void stringAppendString(char *str, size_t maxStrLength, const char *value) {
    int n = maxStrLength - strlen(str) - 1;
    if (n >= 0) {
        strncat(str, value, n);
    }
}
void stringAppendStringLength(char *str, size_t maxStrLength, const char *value, size_t length) {
    int n = MIN(maxStrLength - strlen(str) - 1, length);
    if (n >= 0) {
        strncat(str, value, n);
    }
}
void stringAppendInt(char *str, size_t maxStrLength, int value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%d", value);
}
void stringAppendUInt32(char *str, size_t maxStrLength, uint32_t value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%lu", (unsigned long)value);
}
void stringAppendInt64(char *str, size_t maxStrLength, int64_t value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%jd", value);
}
void stringAppendUInt64(char *str, size_t maxStrLength, uint64_t value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%ju", value);
}
void stringAppendFloat(char *str, size_t maxStrLength, float value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%g", value);
}
void stringAppendFloat(char *str, size_t maxStrLength, float value, int numDecimalPlaces) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%.*f", numDecimalPlaces, value);
}
void stringAppendDouble(char *str, size_t maxStrLength, double value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%g", value);
}
void stringAppendDouble(char *str, size_t maxStrLength, double value, int numDecimalPlaces) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%.*f", numDecimalPlaces, value);
}
void stringAppendVoltage(char *str, size_t maxStrLength, float value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%g V", value);
}
void stringAppendCurrent(char *str, size_t maxStrLength, float value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%g A", value);
}
void stringAppendPower(char *str, size_t maxStrLength, float value) {
    auto n = strlen(str);
    snprintf(str + n, maxStrLength - n, "%g W", value);
}
void stringAppendDuration(char *str, size_t maxStrLength, float value) {
    auto n = strlen(str);
    if (value > 0.1) {
        snprintf(str + n, maxStrLength - n, "%g s", value);
    } else {
        snprintf(str + n, maxStrLength - n, "%g ms", value * 1000);
    }
}
void stringAppendLoad(char *str, size_t maxStrLength, float value) {
    auto n = strlen(str);
    if (value < 1000) {
        snprintf(str + n, maxStrLength - n, "%g ohm", value);
    } else if (value < 1000000) {
        snprintf(str + n, maxStrLength - n, "%g Kohm", value / 1000);
    } else {
        snprintf(str + n, maxStrLength - n, "%g Mohm", value / 1000000);
    }
}
#if defined(EEZ_PLATFORM_STM32) && !defined(EEZ_FOR_LVGL)
uint32_t crc32(const uint8_t *mem_block, size_t block_size) {
	return HAL_CRC_Calculate(&hcrc, (uint32_t *)mem_block, block_size);
}
#else
uint32_t crc32(const uint8_t *mem_block, size_t block_size) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < block_size; ++i) {
        uint32_t byte = mem_block[i]; 
        crc = crc ^ byte;
        for (int j = 0; j < 8; ++j) { 
            uint32_t mask = -((int32_t)crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    return ~crc;
}
#endif
uint8_t toBCD(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}
uint8_t fromBCD(uint8_t bcd) {
    return ((bcd >> 4) & 0xF) * 10 + (bcd & 0xF);
}
float roundPrec(float a, float prec) {
    float r = 1 / prec;
    return roundf(a * r) / r;
}
float floorPrec(float a, float prec) {
    float r = 1 / prec;
    return floorf(a * r) / r;
}
float ceilPrec(float a, float prec) {
    float r = 1 / prec;
    return ceilf(a * r) / r;
}
bool isNaN(float x) {
    return x != x;
}
bool isNaN(double x) {
    return x != x;
}
bool isDigit(char ch) {
    return ch >= '0' && ch <= '9';
}
bool isHexDigit(char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}
bool isUperCaseLetter(char ch) {
    return ch >= 'A' && ch <= 'Z';
}
char toHexDigit(int num) {
    if (num >= 0 && num <= 9) {
        return '0' + num;
    } else {
        return 'A' + (num - 10);
    }
}
int fromHexDigit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    return 10 + (ch - 'A');
}
bool pointInsideRect(int xPoint, int yPoint, int xRect, int yRect, int wRect, int hRect) {
    return xPoint >= xRect && xPoint < xRect + wRect && yPoint >= yRect && yPoint < yRect + hRect;
}
void getParentDir(const char *path, char *parentDirPath) {
    int lastPathSeparatorIndex;
    for (lastPathSeparatorIndex = strlen(path) - 1;
         lastPathSeparatorIndex >= 0 && path[lastPathSeparatorIndex] != PATH_SEPARATOR[0];
         --lastPathSeparatorIndex)
        ;
    int i;
    for (i = 0; i < lastPathSeparatorIndex; ++i) {
        parentDirPath[i] = path[i];
    }
    parentDirPath[i] = 0;
}
bool parseMacAddress(const char *macAddressStr, size_t macAddressStrLength, uint8_t *macAddress) {
    int state = 0;
    int a = 0;
    int i = 0;
    uint8_t resultMacAddress[6];
    const char *end = macAddressStr + macAddressStrLength;
    for (const char *p = macAddressStr; p < end; ++p) {
        if (state == 0) {
            if (*p == '-' || *p == ' ') {
                continue;
            } else if (isHexDigit(*p)) {
                a = fromHexDigit(*p);
                state = 1;
            } else {
                return false;
            }
        } else if (state == 1) {
            if (isHexDigit(*p)) {
                if (i < 6) {
                    resultMacAddress[i++] = (a << 4) | fromHexDigit(*p);
                    state = 0;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
    }
    if (state != 0 || i != 6) {
        return false;
    }
    memcpy(macAddress, resultMacAddress, 6);
    return true;
}
bool parseIpAddress(const char *ipAddressStr, size_t ipAddressStrLength, uint32_t &ipAddress) {
    const char *p = ipAddressStr;
    const char *q = ipAddressStr + ipAddressStrLength;
    uint8_t ipAddressArray[4];
    for (int i = 0; i < 4; ++i) {
        if (p == q) {
            return false;
        }
        uint32_t part = 0;
        for (int j = 0; j < 3; ++j) {
            if (p == q) {
                if (j > 0 && i == 3) {
                    break;
                } else {
                    return false;
                }
            } else if (isDigit(*p)) {
                part = part * 10 + (*p++ - '0');
            } else if (j > 0 && *p == '.') {
                break;
            } else {
                return false;
            }
        }
        if (part > 255) {
            return false;
        }
        if ((i < 3 && *p++ != '.') || (i == 3 && p != q)) {
            return false;
        }
        ipAddressArray[i] = part;
    }
    ipAddress = arrayToIpAddress(ipAddressArray);
    return true;
}
int getIpAddressPartA(uint32_t ipAddress) {
    return ((uint8_t *)&ipAddress)[0];
}
void setIpAddressPartA(uint32_t *ipAddress, uint8_t value) {
    ((uint8_t *)ipAddress)[0] = value;
}
int getIpAddressPartB(uint32_t ipAddress) {
    return ((uint8_t *)&ipAddress)[1];
}
void setIpAddressPartB(uint32_t *ipAddress, uint8_t value) {
    ((uint8_t *)ipAddress)[1] = value;
}
int getIpAddressPartC(uint32_t ipAddress) {
    return ((uint8_t *)&ipAddress)[2];
}
void setIpAddressPartC(uint32_t *ipAddress, uint8_t value) {
    ((uint8_t *)ipAddress)[2] = value;
}
int getIpAddressPartD(uint32_t ipAddress) {
    return ((uint8_t *)&ipAddress)[3];
}
void setIpAddressPartD(uint32_t *ipAddress, uint8_t value) {
    ((uint8_t *)ipAddress)[3] = value;
}
void ipAddressToArray(uint32_t ipAddress, uint8_t *ipAddressArray) {
    ipAddressArray[0] = getIpAddressPartA(ipAddress);
    ipAddressArray[1] = getIpAddressPartB(ipAddress);
    ipAddressArray[2] = getIpAddressPartC(ipAddress);
    ipAddressArray[3] = getIpAddressPartD(ipAddress);
}
uint32_t arrayToIpAddress(uint8_t *ipAddressArray) {
    return getIpAddress(ipAddressArray[0], ipAddressArray[1], ipAddressArray[2], ipAddressArray[3]);
}
uint32_t getIpAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    uint32_t ipAddress;
    setIpAddressPartA(&ipAddress, a);
    setIpAddressPartB(&ipAddress, b);
    setIpAddressPartC(&ipAddress, c);
    setIpAddressPartD(&ipAddress, d);
    return ipAddress;
}
void ipAddressToString(uint32_t ipAddress, char *ipAddressStr, size_t maxIpAddressStrLength) {
    snprintf(ipAddressStr, maxIpAddressStrLength, "%d.%d.%d.%d",
        getIpAddressPartA(ipAddress), getIpAddressPartB(ipAddress),
        getIpAddressPartC(ipAddress), getIpAddressPartD(ipAddress));
}
void macAddressToString(const uint8_t *macAddress, char *macAddressStr) {
    for (int i = 0; i < 6; ++i) {
        macAddressStr[3 * i] = toHexDigit((macAddress[i] & 0xF0) >> 4);
        macAddressStr[3 * i + 1] = toHexDigit(macAddress[i] & 0xF);
        macAddressStr[3 * i + 2] = i < 5 ? '-' : 0;
    }
}
void formatTimeZone(int16_t timeZone, char *text, int count) {
    if (timeZone == 0) {
        stringCopy(text, count, "GMT");
    } else {
        char sign;
        int16_t value;
        if (timeZone > 0) {
            sign = '+';
            value = timeZone;
        } else {
            sign = '-';
            value = -timeZone;
        }
        snprintf(text, count, "%c%02d:%02d GMT", sign, value / 100, value % 100);
    }
}
bool parseTimeZone(const char *timeZoneStr, size_t timeZoneLength, int16_t &timeZone) {
    int state = 0;
    int sign = 1;
    int integerPart = 0;
    int fractionPart = 0;
    const char *end = timeZoneStr + timeZoneLength;
    for (const char *p = timeZoneStr; p < end; ++p) {
        if (*p == ' ') {
            continue;
        }
        if (state == 0) {
            if (*p == '+') {
                state = 1;
            } else if (*p == '-') {
                sign = -1;
                state = 1;
            } else if (isDigit(*p)) {
                integerPart = *p - '0';
                state = 2;
            } else {
                return false;
            }
        } else if (state == 1) {
            if (isDigit(*p)) {
                integerPart = (*p - '0');
                state = 2;
            } else {
                return false;
            }
        } else if (state == 2) {
            if (*p == ':') {
                state = 4;
            } else if (isDigit(*p)) {
                integerPart = integerPart * 10 + (*p - '0');
                state = 3;
            } else {
                return false;
            }
        } else if (state == 3) {
            if (*p == ':') {
                state = 4;
            } else {
                return false;
            }
        } else if (state == 4) {
            if (isDigit(*p)) {
                fractionPart = (*p - '0');
                state = 5;
            } else {
                return false;
            }
        } else if (state == 5) {
            if (isDigit(*p)) {
                fractionPart = fractionPart * 10 + (*p - '0');
                state = 6;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    if (state != 2 && state != 3 && state != 6) {
        return false;
    }
    int value = sign * (integerPart * 100 + fractionPart);
    if (value < -1200 || value > 1400) {
        return false;
    }
    timeZone = (int16_t)value;
    return true;
}
void replaceCharacter(char *str, char ch, char repl) {
    while (*str) {
        if (*str == ch) {
            *str = repl;
        }
        ++str;
    }
}
int strcicmp(char const *a, char const *b) {
    for (;; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0 || !*a)
            return d;
    }
}
int strncicmp(char const *a, char const *b, int n) {
    for (; n--; a++, b++) {
        int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (d != 0 || !*a)
            return d;
    }
    return 0;
}
bool isStringEmpty(char const *s) {
    for (; *s; s++) {
        if (!isspace(*s)) {
            return false;
        }
    }
    return true;
}
bool startsWith(const char *str, const char *prefix) {
    if (!str || !prefix)
        return false;
    size_t strLen = strlen(str);
    size_t prefixLen = strlen(prefix);
    if (prefixLen > strLen)
        return false;
    return strncmp(str, prefix, prefixLen) == 0;
}
bool startsWithNoCase(const char *str, const char *prefix) {
    if (!str || !prefix)
        return false;
    size_t strLen = strlen(str);
    size_t prefixLen = strlen(prefix);
    if (prefixLen > strLen)
        return false;
    return strncicmp(str, prefix, prefixLen) == 0;
}
bool endsWith(const char *str, const char *suffix) {
    if (!str || !suffix)
        return false;
    size_t strLen = strlen(str);
    size_t suffixLen = strlen(suffix);
    if (suffixLen > strLen)
        return false;
    return strncmp(str + strLen - suffixLen, suffix, suffixLen) == 0;
}
bool endsWithNoCase(const char *str, const char *suffix) {
    if (!str || !suffix)
        return false;
    size_t strLen = strlen(str);
    size_t suffixLen = strlen(suffix);
    if (suffixLen > strLen)
        return false;
    return strncicmp(str + strLen - suffixLen, suffix, suffixLen) == 0;
}
void formatBytes(uint64_t bytes, char *text, int count) {
    if (bytes == 0) {
        stringCopy(text, count, "0 Bytes");
    } else {
        double c = 1024.0;
        const char *e[] = { "Bytes", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB" };
        uint64_t f = (uint64_t)floor(log((double)bytes) / log(c));
        double g = round((bytes / pow(c, (double)f)) * 100) / 100;
        snprintf(text, count, "%g %s", g, e[f]);
    }
}
void getFileName(const char *path, char *fileName, unsigned fileNameSize) {
    const char *a = strrchr(path, '/');
    if (a) {
         a++;
    } else {
        a = path;
    }
    const char *b = path + strlen(path);
    unsigned n = b - a;
    n = MIN(fileNameSize - 1, n);
    if (n > 0) {
        memcpy(fileName, a, n);
    }
    fileName[n] = 0;
}
void getBaseFileName(const char *path, char *baseName, unsigned baseNameSize) {
    const char *a = strrchr(path, '/');
    if (a) {
         a++;
    } else {
        a = path;
    }
    const char *b = strrchr(path, '.');
    if (!b || !(b >= a)) {
        b = path + strlen(path);
    }
    unsigned n = b - a;
    n = MIN(baseNameSize - 1, n);
    if (n > 0) {
        memcpy(baseName, a, n);
    }
    baseName[n] = 0;
}
} 
#if defined(M_PI)
static const float PI_FLOAT = (float)M_PI;
#else
static const float PI_FLOAT = (float)3.14159265358979323846;
#endif
static const float c1 = 1.70158f;
static const float c2 = c1 * 1.525f;
static const float c3 = c1 + 1.0f;
static const float c4 = (2 * PI_FLOAT) / 3;
static const float c5 = (2 * PI_FLOAT) / 4.5f;
extern "C" float eez_linear(float x) {
    return x;
}
extern "C" float eez_easeInQuad(float x) {
    return x * x;
}
extern "C" float eez_easeOutQuad(float x) {
    return 1 - (1 - x) * (1 - x);
}
extern "C" float eez_easeInOutQuad(float x) {
    return x < 0.5f ? 2 * x * x : 1 - powf(-2 * x + 2, 2) / 2;
}
extern "C" float eez_easeInCubic(float x) {
    return x * x * x;
}
extern "C" float eez_easeOutCubic(float x) {
    return 1 - pow(1 - x, 3);
}
extern "C" float eez_easeInOutCubic(float x) {
    return x < 0.5f ? 4 * x * x * x : 1 - powf(-2 * x + 2, 3) / 2;
}
extern "C" float eez_easeInQuart(float x) {
    return x * x * x * x;
}
extern "C" float eez_easeOutQuart(float x) {
    return 1 - powf(1 - x, 4);
}
extern "C" float eez_easeInOutQuart(float x) {
    return x < 0.5 ? 8 * x * x * x * x : 1 - powf(-2 * x + 2, 4) / 2;
}
extern "C" float eez_easeInQuint(float x) {
    return x * x * x * x * x;
}
extern "C" float eez_easeOutQuint(float x) {
    return 1 - powf(1 - x, 5);
}
extern "C" float eez_easeInOutQuint(float x) {
    return x < 0.5f ? 16 * x * x * x * x * x : 1 - powf(-2 * x + 2, 5) / 2;
}
extern "C" float eez_easeInSine(float x) {
    return 1 - cosf((x * PI_FLOAT) / 2);
}
extern "C" float eez_easeOutSine(float x) {
    return sinf((x * PI_FLOAT) / 2);
}
extern "C" float eez_easeInOutSine(float x) {
    return -(cosf(PI_FLOAT * x) - 1) / 2;
}
extern "C" float eez_easeInExpo(float x) {
    return x == 0 ? 0 : powf(2, 10 * x - 10);
}
extern "C" float eez_easeOutExpo(float x) {
    return x == 1 ? 1 : 1 - powf(2, -10 * x);
}
extern "C" float eez_easeInOutExpo(float x) {
    return x == 0
        ? 0
        : x == 1
        ? 1
        : x < 0.5
        ? powf(2, 20 * x - 10) / 2
        : (2 - powf(2, -20 * x + 10)) / 2;
}
extern "C" float eez_easeInCirc(float x) {
    return 1 - sqrtf(1 - powf(x, 2));
}
extern "C" float eez_easeOutCirc(float x) {
    return sqrtf(1 - powf(x - 1, 2));
}
extern "C" float eez_easeInOutCirc(float x) {
    return x < 0.5
        ? (1 - sqrtf(1 - pow(2 * x, 2))) / 2
        : (sqrtf(1 - powf(-2 * x + 2, 2)) + 1) / 2;
}
extern "C" float eez_easeInBack(float x) {
    return c3 * x * x * x - c1 * x * x;
}
extern "C" float eez_easeOutBack(float x) {
    return 1 + c3 * powf(x - 1, 3) + c1 * powf(x - 1, 2);
}
extern "C" float eez_easeInOutBack(float x) {
    return x < 0.5
        ? (powf(2 * x, 2) * ((c2 + 1) * 2 * x - c2)) / 2
        : (powf(2 * x - 2, 2) * ((c2 + 1) * (x * 2 - 2) + c2) + 2) / 2;
}
extern "C" float eez_easeInElastic(float x) {
    return x == 0
        ? 0
        : x == 1
        ? 1
        : -powf(2, 10 * x - 10) * sinf((x * 10 - 10.75f) * c4);
}
extern "C" float eez_easeOutElastic(float x) {
    return x == 0
        ? 0
        : x == 1
        ? 1
        : powf(2, -10 * x) * sinf((x * 10 - 0.75f) * c4) + 1;
}
extern "C" float eez_easeInOutElastic(float x) {
    return x == 0
        ? 0
        : x == 1
        ? 1
        : x < 0.5
        ? -(powf(2, 20 * x - 10) * sinf((20 * x - 11.125f) * c5)) / 2
        : (powf(2, -20 * x + 10) * sinf((20 * x - 11.125f) * c5)) / 2 + 1;
}
extern "C" float eez_easeOutBounce(float x);
extern "C" float eez_easeInBounce(float x) {
    return 1 - eez_easeOutBounce(1 - x);
}
extern "C" float eez_easeOutBounce(float x) {
    static const float n1 = 7.5625f;
    static const float d1 = 2.75f;
    if (x < 1 / d1) {
        return n1 * x * x;
    } else if (x < 2 / d1) {
        x -= 1.5f / d1;
        return n1 * x * x + 0.75f;
    } else if (x < 2.5f / d1) {
        x -= 2.25f / d1;
        return n1 * x * x + 0.9375f;
    } else {
        x -= 2.625f / d1;
        return n1 * x * x + 0.984375f;
    }
};
extern "C" float eez_easeInOutBounce(float x) {
    return x < 0.5
        ? (1 - eez_easeOutBounce(1 - 2 * x)) / 2
        : (1 + eez_easeOutBounce(2 * x - 1)) / 2;
}
namespace eez {
EasingFuncType g_easingFuncs[] = {
    eez_linear,
    eez_easeInQuad,
    eez_easeOutQuad,
    eez_easeInOutQuad,
    eez_easeInCubic,
    eez_easeOutCubic,
    eez_easeInOutCubic,
    eez_easeInQuart,
    eez_easeOutQuart,
    eez_easeInOutQuart,
    eez_easeInQuint,
    eez_easeOutQuint,
    eez_easeInOutQuint,
    eez_easeInSine,
    eez_easeOutSine,
    eez_easeInOutSine,
    eez_easeInExpo,
    eez_easeOutExpo,
    eez_easeInOutExpo,
    eez_easeInCirc,
    eez_easeOutCirc,
    eez_easeInOutCirc,
    eez_easeInBack,
    eez_easeOutBack,
    eez_easeInOutBack,
    eez_easeInElastic,
    eez_easeOutElastic,
    eez_easeInOutElastic,
    eez_easeInBounce,
    eez_easeOutBounce,
    eez_easeInOutBounce,
};
} 
#ifdef EEZ_PLATFORM_SIMULATOR_WIN32
char *strnstr(const char *s1, const char *s2, size_t n) {
    char c = *s2;
    if (c == '\0')
        return (char *)s1;
    for (size_t len = strlen(s2); len <= n; n--, s1++) {
        if (*s1 == c) {
            for (size_t i = 1;; i++) {
                if (i == len) {
                    return (char *)s1;
                }
                if (s1[i] != s2[i]) {
                    break;
                }
            }
        }
    }
    return NULL;
}
#endif
// -----------------------------------------------------------------------------
// core/value.cpp
// -----------------------------------------------------------------------------
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#if defined(EEZ_DASHBOARD_API)
#endif
namespace eez {
bool compare_UNDEFINED_value(const Value &a, const Value &b) {
    return b.type == VALUE_TYPE_UNDEFINED && a.int32Value == b.int32Value;
}
void UNDEFINED_value_to_text(const Value &value, char *text, int count) {
    *text = 0;
}
const char *UNDEFINED_value_type_name(const Value &value) {
    return "undefined";
}
bool compare_NULL_value(const Value &a, const Value &b) {
    return b.type == VALUE_TYPE_NULL;
}
void NULL_value_to_text(const Value &value, char *text, int count) {
    *text = 0;
}
const char *NULL_value_type_name(const Value &value) {
    return "null";
}
bool compare_BOOLEAN_value(const Value &a, const Value &b) {
    return a.getInt() == b.getInt();
}
void BOOLEAN_value_to_text(const Value &value, char *text, int count) {
    if (value.getInt()) {
        stringCopy(text, count, "true");
    } else {
        stringCopy(text, count, "false");
    }
}
const char *BOOLEAN_value_type_name(const Value &value) {
    return "boolean";
}
bool compare_INT8_value(const Value &a, const Value &b) {
    return a.getInt8() == b.getInt8();
}
void INT8_value_to_text(const Value &value, char *text, int count) {
    stringAppendInt(text, count, value.getInt8());
}
const char *INT8_value_type_name(const Value &value) {
    return "int8";
}
bool compare_UINT8_value(const Value &a, const Value &b) {
    return a.getUInt8() == b.getUInt8();
}
void UINT8_value_to_text(const Value &value, char *text, int count) {
    stringAppendUInt32(text, count, value.getUInt8());
}
const char *UINT8_value_type_name(const Value &value) {
    return "uint8";
}
bool compare_INT16_value(const Value &a, const Value &b) {
    return a.getInt16() == b.getInt16();
}
void INT16_value_to_text(const Value &value, char *text, int count) {
    stringAppendInt(text, count, value.getInt16());
}
const char *INT16_value_type_name(const Value &value) {
    return "int16";
}
bool compare_UINT16_value(const Value &a, const Value &b) {
    return a.getUInt16() == b.getUInt16();
}
void UINT16_value_to_text(const Value &value, char *text, int count) {
    stringAppendUInt32(text, count, value.getUInt16());
}
const char *UINT16_value_type_name(const Value &value) {
    return "uint16";
}
bool compare_INT32_value(const Value &a, const Value &b) {
    return a.getInt32() == b.getInt32();
}
void INT32_value_to_text(const Value &value, char *text, int count) {
    stringAppendInt(text, count, value.getInt32());
}
const char *INT32_value_type_name(const Value &value) {
    return "int32";
}
bool compare_UINT32_value(const Value &a, const Value &b) {
    return a.getUInt32() == b.getUInt32();
}
void UINT32_value_to_text(const Value &value, char *text, int count) {
    stringAppendUInt32(text, count, value.getUInt32());
}
const char *UINT32_value_type_name(const Value &value) {
    return "uint32";
}
bool compare_INT64_value(const Value &a, const Value &b) {
    return a.getInt64() == b.getInt64();
}
void INT64_value_to_text(const Value &value, char *text, int count) {
    stringAppendInt64(text, count, value.getInt64());
}
const char *INT64_value_type_name(const Value &value) {
    return "int64";
}
bool compare_UINT64_value(const Value &a, const Value &b) {
    return a.getUInt64() == b.getUInt64();
}
void UINT64_value_to_text(const Value &value, char *text, int count) {
    stringAppendUInt64(text, count, value.getUInt64());
}
const char *UINT64_value_type_name(const Value &value) {
    return "uint64";
}
bool compare_FLOAT_value(const Value &a, const Value &b) {
    return a.getUnit() == b.getUnit() && a.getFloat() == b.getFloat() && a.getOptions() == b.getOptions();
}
void FLOAT_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
    float floatValue = value.getFloat();
#if defined(INFINITY_SYMBOL)
    if (isinf(floatValue)) {
        snprintf(text, count, INFINITY_SYMBOL);
        return;
    }
#endif
    Unit unit = value.getUnit();
    bool appendDotZero = unit == UNIT_VOLT || unit == UNIT_VOLT_PP || unit == UNIT_AMPER || unit == UNIT_AMPER_PP || unit == UNIT_WATT;
    uint16_t options = value.getOptions();
    bool fixedDecimals = (options & FLOAT_OPTIONS_FIXED_DECIMALS) != 0;
    if (floatValue != 0) {
        if (!fixedDecimals) {
            unit = findDerivedUnit(floatValue, unit);
            floatValue /= getUnitFactor(unit);
        }
    } else {
        floatValue = 0; 
    }
    if (!isNaN(floatValue)) {
        if ((value.getOptions() & FLOAT_OPTIONS_LESS_THEN) != 0) {
            stringAppendString(text, count, "< ");
			appendDotZero = false;
        }
        if (fixedDecimals) {
            stringAppendFloat(text, count, floatValue, FLOAT_OPTIONS_GET_NUM_FIXED_DECIMALS(options));
        } else {
            if (unit == UNIT_WATT || unit == UNIT_MILLI_WATT) {
                stringAppendFloat(text, count, floatValue, 2);
            } else {
                stringAppendFloat(text, count, floatValue);
            }
            int n = strlen(text);
            int decimalPointIndex;
            for (decimalPointIndex = 0; decimalPointIndex < n; ++decimalPointIndex) {
                if (text[decimalPointIndex] == '.') {
                    break;
                }
            }
            if (decimalPointIndex == n) {
                if (appendDotZero) {
                    stringAppendString(text, count, ".0");
                }
            } else if (decimalPointIndex == n - 1) {
                if (appendDotZero) {
                    stringAppendString(text, count, "0");
                } else {
                    text[decimalPointIndex] = 0;
                }
            } else {
                if (appendDotZero) {
                    for (int j = n - 1; j > decimalPointIndex + 1 && text[j] == '0'; j--) {
                        text[j] = 0;
                    }
                } else {
                    for (int j = n - 1; j >= decimalPointIndex && (text[j] == '0' || text[j] == '.'); j--) {
                        text[j] = 0;
                    }
                }
            }
        }
        const char *unitName = getUnitName(unit);
        if (unitName && *unitName) {
            stringAppendString(text, count, " ");
            stringAppendString(text, count, unitName);
        }
    } else {
        text[0] = 0;
    }
}
const char *FLOAT_value_type_name(const Value &value) {
    return "float";
}
bool compare_DOUBLE_value(const Value &a, const Value &b) {
    return a.getUnit() == b.getUnit() && a.getDouble() == b.getDouble() && a.getOptions() == b.getOptions();
}
void DOUBLE_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
    double doubleValue = value.getDouble();
#if defined(INFINITY_SYMBOL)
    if (isinf(doubleValue)) {
        snprintf(text, count, INFINITY_SYMBOL);
        return;
    }
#endif
    Unit unit = value.getUnit();
    bool appendDotZero = unit == UNIT_VOLT || unit == UNIT_VOLT_PP || unit == UNIT_AMPER || unit == UNIT_AMPER_PP || unit == UNIT_WATT;
    uint16_t options = value.getOptions();
    bool fixedDecimals = (options & FLOAT_OPTIONS_FIXED_DECIMALS) != 0;
    if (doubleValue != 0) {
        if (!fixedDecimals) {
            unit = findDerivedUnit(fabs(doubleValue), unit);
            doubleValue /= getUnitFactor(unit);
        }
    } else {
        doubleValue = 0; 
    }
    if (!isNaN(doubleValue)) {
        if ((value.getOptions() & FLOAT_OPTIONS_LESS_THEN) != 0) {
            stringAppendString(text, count, "< ");
			appendDotZero = false;
        }
        if (fixedDecimals) {
            stringAppendFloat(text, count, doubleValue, FLOAT_OPTIONS_GET_NUM_FIXED_DECIMALS(options));
        } else {
            if (unit == UNIT_WATT || unit == UNIT_MILLI_WATT) {
                stringAppendDouble(text, count, doubleValue, 2);
            } else {
                stringAppendDouble(text, count, doubleValue);
            }
            int n = strlen(text);
            int decimalPointIndex;
            for (decimalPointIndex = 0; decimalPointIndex < n; ++decimalPointIndex) {
                if (text[decimalPointIndex] == '.') {
                    break;
                }
            }
            if (decimalPointIndex == n) {
                if (appendDotZero) {
                    stringAppendString(text, count, ".0");
                }
            } else if (decimalPointIndex == n - 1) {
                if (appendDotZero) {
                    stringAppendString(text, count, "0");
                } else {
                    text[decimalPointIndex] = 0;
                }
            } else {
                if (appendDotZero) {
                    for (int j = n - 1; j > decimalPointIndex + 1 && text[j] == '0'; j--) {
                        text[j] = 0;
                    }
                } else {
                    for (int j = n - 1; j >= decimalPointIndex && (text[j] == '0' || text[j] == '.'); j--) {
                        text[j] = 0;
                    }
                }
            }
        }
        const char *unitName = getUnitName(unit);
        if (unitName && *unitName) {
            stringAppendString(text, count, " ");
            stringAppendString(text, count, unitName);
        }
    } else {
        text[0] = 0;
    }
}
const char *DOUBLE_value_type_name(const Value &value) {
    return "double";
}
bool compare_STRING_value(const Value &a, const Value &b) {
    const char *astr = a.getString();
    const char *bstr = b.getString();
    if (!astr && !bstr) {
        return true;
    }
    if ((!astr && bstr) || (astr && !bstr)) {
        return false;
    }
    return strcmp(astr, bstr) == 0;
}
void STRING_value_to_text(const Value &value, char *text, int count) {
    const char *str = value.getString();
    if (str) {
        stringCopy(text, count, str);
    } else {
        text[0] = 0;
    }
}
const char *STRING_value_type_name(const Value &value) {
    return "string";
}
bool compare_STRING_ASSET_value(const Value &a, const Value &b) {
    return compare_STRING_value(a, b);
}
void STRING_ASSET_value_to_text(const Value &value, char *text, int count) {
    STRING_value_to_text(value, text, count);
}
const char *STRING_ASSET_value_type_name(const Value &value) {
    return "string";
}
bool compare_ARRAY_value(const Value &a, const Value &b) {
    return a.arrayValue == b.arrayValue;
}
void ARRAY_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
}
const char *ARRAY_value_type_name(const Value &value) {
    return "array";
}
bool compare_ARRAY_ASSET_value(const Value &a, const Value &b) {
    return a.int32Value == b.int32Value;
}
void ARRAY_ASSET_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
}
const char *ARRAY_ASSET_value_type_name(const Value &value) {
    return "array";
}
bool compare_ARRAY_REF_value(const Value &a, const Value &b) {
    return a.refValue == b.refValue;
}
void ARRAY_REF_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
}
const char *ARRAY_REF_value_type_name(const Value &value) {
    return "array";
}
bool compare_STRING_REF_value(const Value &a, const Value &b) {
	return compare_STRING_value(a, b);
}
void STRING_REF_value_to_text(const Value &value, char *text, int count) {
	STRING_value_to_text(value, text, count);
}
const char *STRING_REF_value_type_name(const Value &value) {
    return "string";
}
bool compare_BLOB_REF_value(const Value &a, const Value &b) {
    return a.refValue == b.refValue;
}
void BLOB_REF_value_to_text(const Value &value, char *text, int count) {
    snprintf(text, count, "blob (size=%d)", value.getInt());
}
const char *BLOB_REF_value_type_name(const Value &value) {
    return "blob";
}
bool compare_STREAM_value(const Value &a, const Value &b) {
    return a.int32Value == b.int32Value;
}
void STREAM_value_to_text(const Value &value, char *text, int count) {
    snprintf(text, count, "stream (id=%d)", value.getInt());
}
const char *STREAM_value_type_name(const Value &value) {
    return "stream";
}
bool compare_WIDGET_value(const Value &a, const Value &b) {
    return a.int32Value == b.int32Value;
}
void WIDGET_value_to_text(const Value &value, char *text, int count) {
    snprintf(text, count, "widget (id=%d)", value.getInt());
}
const char *WIDGET_value_type_name(const Value &value) {
    return "widget";
}
bool compare_JSON_value(const Value &a, const Value &b) {
    return a.int32Value == b.int32Value;
}
void JSON_value_to_text(const Value &value, char *text, int count) {
    snprintf(text, count, "json (id=%d)", value.getInt());
}
const char *JSON_value_type_name(const Value &value) {
    return "json";
}
bool compare_JSON_MEMBER_VALUE_value(const Value &a, const Value &b) {
	return a.getValue() == b.getValue();
}
void JSON_MEMBER_VALUE_value_to_text(const Value &value, char *text, int count) {
	value.getValue().toText(text, count);
}
const char *JSON_MEMBER_VALUE_value_type_name(const Value &value) {
    auto value2 = value.getValue();
    return g_valueTypeNames[value2.type](value2);
}
bool compare_DATE_value(const Value &a, const Value &b) {
    return a.doubleValue == b.doubleValue;
}
void DATE_value_to_text(const Value &value, char *text, int count) {
    flow::date::toLocaleString((flow::date::Date)value.getDouble(), text, count);
}
const char *DATE_value_type_name(const Value &value) {
    return "date";
}
bool compare_VERSIONED_STRING_value(const Value &a, const Value &b) {
    return a.unit == b.unit; 
}
void VERSIONED_STRING_value_to_text(const Value &value, char *text, int count) {
    const char *str = value.getString();
    if (str) {
        stringCopy(text, count, str);
    } else {
        text[0] = 0;
    }
}
const char *VERSIONED_STRING_value_type_name(const Value &value) {
    return "versioned-string";
}
bool compare_VALUE_PTR_value(const Value &a, const Value &b) {
	return a.pValueValue == b.pValueValue || (a.pValueValue && b.pValueValue && *a.pValueValue == *b.pValueValue);
}
void VALUE_PTR_value_to_text(const Value &value, char *text, int count) {
	if (value.pValueValue) {
		value.pValueValue->toText(text, count);
	} else {
		text[0] = 0;
	}
}
const char *VALUE_PTR_value_type_name(const Value &value) {
	if (value.pValueValue) {
		return g_valueTypeNames[value.pValueValue->type](value.pValueValue);
	} else {
		return "null";
	}
}
bool compare_ARRAY_ELEMENT_VALUE_value(const Value &a, const Value &b) {
	return a.getValue() == b.getValue();
}
void ARRAY_ELEMENT_VALUE_value_to_text(const Value &value, char *text, int count) {
	value.getValue().toText(text, count);
}
const char *ARRAY_ELEMENT_VALUE_value_type_name(const Value &value) {
    auto value2 = value.getValue();
    return g_valueTypeNames[value2.type](value2);
}
bool compare_FLOW_OUTPUT_value(const Value &a, const Value &b) {
	return a.getUInt16() == b.getUInt16();
}
void FLOW_OUTPUT_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
}
const char *FLOW_OUTPUT_value_type_name(const Value &value) {
    return "internal";
}
#if EEZ_OPTION_GUI
using namespace gui;
bool compare_NATIVE_VARIABLE_value(const Value &a, const Value &b) {
    auto aValue = get(g_widgetCursor, a.getInt());
    auto bValue = get(g_widgetCursor, b.getInt());
	return aValue == bValue;
}
void NATIVE_VARIABLE_value_to_text(const Value &value, char *text, int count) {
    auto aValue = get(g_widgetCursor, value.getInt());
    aValue.toText(text, count);
}
const char *NATIVE_VARIABLE_value_type_name(const Value &value) {
    auto aValue = get(g_widgetCursor, value.getInt());
    return g_valueTypeNames[aValue.type](aValue);
}
#else
bool compare_NATIVE_VARIABLE_value(const Value &a, const Value &b) {
	return false;
}
void NATIVE_VARIABLE_value_to_text(const Value &value, char *text, int count) {
    *text = 0;
}
const char *NATIVE_VARIABLE_value_type_name(const Value &value) {
    return "";
}
#endif
bool compare_ERROR_value(const Value &a, const Value &b) {
	return false;
}
void ERROR_value_to_text(const Value &value, char *text, int count) {
    *text = 0;
}
const char *ERROR_value_type_name(const Value &value) {
    return "error";
}
bool compare_RANGE_value(const Value &a, const Value &b) {
    return a.getUInt32() == b.getUInt32();
}
void RANGE_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
}
const char *RANGE_value_type_name(const Value &value) {
    return "internal";
}
bool compare_POINTER_value(const Value &a, const Value &b) {
    return a.getVoidPointer() == b.getVoidPointer();
}
void POINTER_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
}
const char *POINTER_value_type_name(const Value &value) {
    return "internal";
}
#if EEZ_OPTION_GUI
using namespace gui;
bool compare_ENUM_value(const Value &a, const Value &b) {
    return a.getEnum().enumDefinition == b.getEnum().enumDefinition &&
           a.getEnum().enumValue == b.getEnum().enumValue;
}
void ENUM_value_to_text(const Value &value, char *text, int count) {
    const EnumItem *enumDefinition = g_enumDefinitions[value.getEnum().enumDefinition];
    for (int i = 0; enumDefinition[i].menuLabel; ++i) {
        if (value.getEnum().enumValue == enumDefinition[i].value) {
            if (enumDefinition[i].widgetLabel) {
                stringCopy(text, count, enumDefinition[i].widgetLabel);
            } else {
                stringCopy(text, count, enumDefinition[i].menuLabel);
            }
            break;
        }
    }
}
const char *ENUM_value_type_name(const Value &value) {
    return "internal";
}
#else
bool compare_ENUM_value(const Value &a, const Value &b) {
    return false;
}
void ENUM_value_to_text(const Value &value, char *text, int count) {
    *text = 0;
}
const char *ENUM_value_type_name(const Value &value) {
    return "internal";
}
#endif 
bool compare_YT_DATA_GET_VALUE_FUNCTION_POINTER_value(const Value &a, const Value &b) {
    return a.getUInt32() == b.getUInt32();
}
bool compare_IP_ADDRESS_value(const Value &a, const Value &b) {
    return a.getUInt32() == b.getUInt32();
}
void IP_ADDRESS_value_to_text(const Value &value, char *text, int count) {
    ipAddressToString(value.getUInt32(), text, count);
}
const char *IP_ADDRESS_value_type_name(const Value &value) {
    return "internal";
}
bool compare_TIME_ZONE_value(const Value &a, const Value &b) {
    return a.getInt16() == b.getInt16();
}
void TIME_ZONE_value_to_text(const Value &value, char *text, int count) {
    formatTimeZone(value.getInt16(), text, count);
}
const char *TIME_ZONE_value_type_name(const Value &value) {
    return "internal";
}
void YT_DATA_GET_VALUE_FUNCTION_POINTER_value_to_text(const Value &value, char *text, int count) {
    text[0] = 0;
}
const char *YT_DATA_GET_VALUE_FUNCTION_POINTER_value_type_name(const Value &value) {
    return "internal";
}
#define VALUE_TYPE(NAME) bool compare_##NAME##_value(const Value &a, const Value &b);
VALUE_TYPES
#undef VALUE_TYPE
#define VALUE_TYPE(NAME) compare_##NAME##_value,
CompareValueFunction g_valueTypeCompareFunctions[] = {
	VALUE_TYPES
};
#undef VALUE_TYPE
#define VALUE_TYPE(NAME) void NAME##_value_to_text(const Value &value, char *text, int count);
VALUE_TYPES
#undef VALUE_TYPE
#define VALUE_TYPE(NAME) NAME##_value_to_text,
ValueToTextFunction g_valueTypeToTextFunctions[] = {
	VALUE_TYPES
};
#undef VALUE_TYPE
#define VALUE_TYPE(NAME) const char * NAME##_value_type_name(const Value &value);
VALUE_TYPES
#undef VALUE_TYPE
#define VALUE_TYPE(NAME) NAME##_value_type_name,
ValueTypeNameFunction g_valueTypeNames[] = {
	VALUE_TYPES
};
#undef VALUE_TYPE
ArrayValueRef::~ArrayValueRef() {
    eez::flow::onArrayValueFree(&arrayValue);
    for (uint32_t i = 1; i < arrayValue.arraySize; i++) {
        (arrayValue.values + i)->~Value();
    }
}
bool assignValue(Value &dstValue, const Value &srcValue, uint32_t dstValueType) {
    if (dstValue.isBoolean()) {
        dstValue.int32Value = srcValue.toBool();
    } else if (dstValue.isInt32OrLess()) {
        dstValue.int32Value = srcValue.toInt32();
    } else if (dstValue.isFloat()) {
        dstValue.floatValue = srcValue.toFloat();
    } else if (dstValue.isDouble()) {
        dstValue.doubleValue = srcValue.toDouble();
    } else if (dstValue.isString()) {
        dstValue = srcValue.toString(0x30a91156);
    } else {
#if defined(EEZ_DASHBOARD_API)
        if (dstValueType == VALUE_TYPE_JSON && !srcValue.isJson()) {
            dstValue = flow::convertToJson(&srcValue);
        } else if (srcValue.isJson() && dstValueType != VALUE_TYPE_JSON) {
            dstValue = flow::convertFromJson(srcValue.getInt(), dstValueType);
        } else {
#endif
            dstValue = srcValue;
#if defined(EEZ_DASHBOARD_API)
        }
#endif
    }
    return true;
}
uint16_t getPageIndexFromValue(const Value &value) {
    return value.getFirstUInt16();
}
uint16_t getNumPagesFromValue(const Value &value) {
    return value.getSecondUInt16();
}
Value MakeRangeValue(uint16_t from, uint16_t to) {
    Value value;
    value.type = VALUE_TYPE_RANGE;
    value.pairOfUint16Value.first = from;
    value.pairOfUint16Value.second = to;
    return value;
}
Value MakeEnumDefinitionValue(uint8_t enumValue, uint8_t enumDefinition) {
    Value value;
    value.type = VALUE_TYPE_ENUM;
    value.enumValue.enumValue = enumValue;
    value.enumValue.enumDefinition = enumDefinition;
    return value;
}
const char *Value::getString() const {
    auto value = getValue(); 
	if (value.type == VALUE_TYPE_STRING_REF) {
		return ((StringRef *)value.refValue)->str;
	}
	if (value.type == VALUE_TYPE_STRING) {
		return value.strValue;
	}
	return nullptr;
}
const ArrayValue *Value::getArray() const {
    if (type == VALUE_TYPE_ARRAY) {
        return arrayValue;
    }
    if (type == VALUE_TYPE_ARRAY_ASSET) {
        return (ArrayValue *)((uint8_t *)&int32Value + int32Value);
    }
    return &((ArrayValueRef *)refValue)->arrayValue;
}
ArrayValue *Value::getArray() {
    if (type == VALUE_TYPE_ARRAY) {
        return arrayValue;
    }
    if (type == VALUE_TYPE_ARRAY_ASSET) {
        return (ArrayValue *)((uint8_t *)&int32Value + int32Value);
    }
    return &((ArrayValueRef *)refValue)->arrayValue;
}
double Value::toDouble(int *err) const {
	if (isIndirectValueType()) {
		return getValue().toDouble(err);
	}
	if (err) {
		*err = 0;
	}
	if (type == VALUE_TYPE_DOUBLE) {
		return doubleValue;
	}
	if (type == VALUE_TYPE_FLOAT) {
		return floatValue;
	}
	if (type == VALUE_TYPE_INT8) {
		return int8Value;
	}
	if (type == VALUE_TYPE_UINT8) {
		return uint8Value;
	}
	if (type == VALUE_TYPE_INT16) {
		return int16Value;
	}
	if (type == VALUE_TYPE_UINT16) {
		return uint16Value;
	}
	if (type == VALUE_TYPE_INT32 || type == VALUE_TYPE_BOOLEAN) {
		return int32Value;
	}
	if (type == VALUE_TYPE_UINT32) {
		return uint32Value;
	}
	if (type == VALUE_TYPE_INT64) {
		return (double)int64Value;
	}
	if (type == VALUE_TYPE_UINT64) {
		return (double)uint64Value;
	}
	if (type == VALUE_TYPE_DATE) {
		return doubleValue;
	}
	if (isString()) {
        const char *pStart = getString();
        char *pEnd;
		double value = strtod(pStart, &pEnd);
        while (isspace(*pEnd)) {
            pEnd++;
        }
        if (*pEnd == '\0') {
            return value;
        }
	}
    if (err) {
        *err = 1;
    }
	return NAN;
}
float Value::toFloat(int *err) const {
	if (isIndirectValueType()) {
		return getValue().toFloat(err);
	}
	if (err) {
		*err = 0;
	}
	if (type == VALUE_TYPE_DOUBLE) {
		return (float)doubleValue;
	}
	if (type == VALUE_TYPE_FLOAT) {
		return floatValue;
	}
	if (type == VALUE_TYPE_INT8) {
		return int8Value;
	}
	if (type == VALUE_TYPE_UINT8) {
		return uint8Value;
	}
	if (type == VALUE_TYPE_INT16) {
		return int16Value;
	}
	if (type == VALUE_TYPE_UINT16) {
		return uint16Value;
	}
	if (type == VALUE_TYPE_INT32 || type == VALUE_TYPE_BOOLEAN) {
		return (float)int32Value;
	}
	if (type == VALUE_TYPE_UINT32) {
		return (float)uint32Value;
	}
	if (type == VALUE_TYPE_INT64) {
		return (float)int64Value;
	}
	if (type == VALUE_TYPE_UINT64) {
		return (float)uint64Value;
	}
	if (isString()) {
        const char *pStart = getString();
        char *pEnd;
		float value = strtof(pStart, &pEnd);
        while (isspace(*pEnd)) {
            pEnd++;
        }
        if (*pEnd == '\0') {
            return value;
        }
	}
    if (err) {
        *err = 1;
    }
    return NAN;
}
int32_t Value::toInt32(int *err) const {
	if (isIndirectValueType()) {
		return getValue().toInt32(err);
	}
	if (err) {
		*err = 0;
	}
	if (type == VALUE_TYPE_INT32 || type == VALUE_TYPE_BOOLEAN) {
		return int32Value;
	}
	if (type == VALUE_TYPE_UINT32) {
		return (int32_t)uint32Value;
	}
	if (type == VALUE_TYPE_INT8) {
		return int8Value;
	}
	if (type == VALUE_TYPE_UINT8) {
		return uint8Value;
	}
	if (type == VALUE_TYPE_INT16) {
		return int16Value;
	}
	if (type == VALUE_TYPE_UINT16) {
		return uint16Value;
	}
	if (type == VALUE_TYPE_INT64) {
		return (int32_t)int64Value;
	}
	if (type == VALUE_TYPE_UINT64) {
		return (int32_t)uint64Value;
	}
	if (type == VALUE_TYPE_VALUE_PTR) {
		return pValueValue->toInt32(err);
	}
	if (type == VALUE_TYPE_DOUBLE) {
		return (int32_t)doubleValue;
	}
	if (type == VALUE_TYPE_FLOAT) {
		return (int32_t)floatValue;
	}
	if (isString()) {
        const char *pStart = getString();
        char *pEnd;
		int value = strtol(pStart, &pEnd, 10);
        while (isspace(*pEnd)) {
            pEnd++;
        }
        if (*pEnd == '\0') {
            return value;
        }
	}
    if (err) {
        *err = 1;
    }
	return 0;
}
int64_t Value::toInt64(int *err) const {
	if (isIndirectValueType()) {
		return getValue().toInt64(err);
	}
	if (err) {
		*err = 0;
	}
	if (type == VALUE_TYPE_DOUBLE) {
		return (int64_t)doubleValue;
	}
	if (type == VALUE_TYPE_FLOAT) {
		return (int64_t)floatValue;
	}
	if (type == VALUE_TYPE_INT8) {
		return int8Value;
	}
	if (type == VALUE_TYPE_UINT8) {
		return uint8Value;
	}
	if (type == VALUE_TYPE_INT16) {
		return int16Value;
	}
	if (type == VALUE_TYPE_UINT16) {
		return uint16Value;
	}
	if (type == VALUE_TYPE_INT32 || type == VALUE_TYPE_BOOLEAN) {
		return int32Value;
	}
	if (type == VALUE_TYPE_UINT32) {
		return uint32Value;
	}
	if (type == VALUE_TYPE_INT64) {
		return int64Value;
	}
	if (type == VALUE_TYPE_UINT64) {
		return (int64_t)uint64Value;
	}
	if (isString()) {
        const char *pStart = getString();
        char *pEnd;
		int64_t value = strtol(pStart, &pEnd, 10);
        while (isspace(*pEnd)) {
            pEnd++;
        }
        if (*pEnd == '\0') {
            return value;
        }
	}
    if (err) {
        *err = 1;
    }
	return 0;
}
bool Value::toBool(int *err) const {
	if (isIndirectValueType()) {
		return getValue().toBool(err);
	}
    if (err) {
		*err = 0;
	}
	if (type == VALUE_TYPE_UNDEFINED || type == VALUE_TYPE_NULL) {
		return false;
	}
	if (type == VALUE_TYPE_DOUBLE) {
		return doubleValue != 0;
	}
	if (type == VALUE_TYPE_FLOAT) {
		return floatValue != 0;
	}
	if (type == VALUE_TYPE_INT8) {
		return int8Value != 0;
	}
	if (type == VALUE_TYPE_UINT8) {
		return uint8Value != 0;
	}
	if (type == VALUE_TYPE_INT16) {
		return int16Value != 0;
	}
	if (type == VALUE_TYPE_UINT16) {
		return uint16Value != 0;
	}
	if (type == VALUE_TYPE_INT32 || type == VALUE_TYPE_BOOLEAN) {
		return int32Value != 0;
	}
	if (type == VALUE_TYPE_UINT32) {
		return uint32Value != 0;
	}
	if (type == VALUE_TYPE_INT64) {
		return int64Value != 0;
	}
	if (type == VALUE_TYPE_UINT64) {
		return uint64Value != 0;
	}
	if (type == VALUE_TYPE_DATE) {
		return doubleValue != 0;
	}
	if (isString()) {
		const char *str = getString();
		return str && *str;
	}
	if (isBlob()) {
		auto blobRef = getBlob();
		return blobRef->len > 0;
	}
	if (isArray()) {
		auto arrayValue = getArray();
        return arrayValue->arraySize != 0;
	}
    if (isJson()) {
        return int32Value != 0;
    }
    if (err) {
        *err = 1;
    }
	return false;
}
Value Value::toString(uint32_t id) const {
	if (isIndirectValueType()) {
		return getValue().toString(id);
	}
	if (isString()) {
		return *this;
	}
    char tempStr[64];
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4474)
#endif
    if (type == VALUE_TYPE_DOUBLE) {
        snprintf(tempStr, sizeof(tempStr), "%g", doubleValue);
    } else if (type == VALUE_TYPE_FLOAT) {
        snprintf(tempStr, sizeof(tempStr), "%g", floatValue);
    } else if (type == VALUE_TYPE_INT8) {
        snprintf(tempStr, sizeof(tempStr), "%" PRId8 "", int8Value);
    } else if (type == VALUE_TYPE_UINT8) {
        snprintf(tempStr, sizeof(tempStr), "%" PRIu8 "", uint8Value);
    } else if (type == VALUE_TYPE_INT16) {
        snprintf(tempStr, sizeof(tempStr), "%" PRId16 "", int16Value);
    } else if (type == VALUE_TYPE_UINT16) {
        snprintf(tempStr, sizeof(tempStr), "%" PRIu16 "", uint16Value);
    } else if (type == VALUE_TYPE_INT32) {
        snprintf(tempStr, sizeof(tempStr), "%" PRId32 "", int32Value);
    } else if (type == VALUE_TYPE_UINT32) {
        snprintf(tempStr, sizeof(tempStr), "%" PRIu32 "", uint32Value);
    } else if (type == VALUE_TYPE_INT64) {
        snprintf(tempStr, sizeof(tempStr), "%" PRId64 "", int64Value);
    } else if (type == VALUE_TYPE_UINT64) {
        snprintf(tempStr, sizeof(tempStr), "%" PRIu64 "", uint64Value);
    } else {
        toText(tempStr, sizeof(tempStr));
    }
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	return makeStringRef(tempStr, strlen(tempStr), id);
}
Value Value::makeStringRef(const char *str, int len, uint32_t id) {
    auto stringRef = ObjectAllocator<StringRef>::allocate(id);
	if (stringRef == nullptr) {
		return Value(0, VALUE_TYPE_NULL);
	}
	if (len == -1) {
		len = strlen(str);
	}
    stringRef->str = (char *)alloc(len + 1, id + 1);
    if (stringRef->str == nullptr) {
        ObjectAllocator<StringRef>::deallocate(stringRef);
        return Value(0, VALUE_TYPE_NULL);
    }
    stringCopyLength(stringRef->str, len + 1, str, len);
	stringRef->str[len] = 0;
    stringRef->refCounter = 1;
    Value value;
    value.type = VALUE_TYPE_STRING_REF;
    value.options = VALUE_OPTIONS_REF;
    value.refValue = stringRef;
	return value;
}
Value Value::concatenateString(const Value &str1, const Value &str2) {
    auto stringRef = ObjectAllocator<StringRef>::allocate(0xbab14c6a);;
	if (stringRef == nullptr) {
		return Value(0, VALUE_TYPE_NULL);
	}
    auto newStrLen = strlen(str1.getString()) + strlen(str2.getString()) + 1;
    stringRef->str = (char *)alloc(newStrLen, 0xb5320162);
    if (stringRef->str == nullptr) {
        ObjectAllocator<StringRef>::deallocate(stringRef);
        return Value(0, VALUE_TYPE_NULL);
    }
    stringCopy(stringRef->str, newStrLen, str1.getString());
    stringAppendString(stringRef->str, newStrLen, str2.getString());
    stringRef->refCounter = 1;
    Value value;
    value.type = VALUE_TYPE_STRING_REF;
    value.options = VALUE_OPTIONS_REF;
    value.refValue = stringRef;
	return value;
}
Value Value::makeArrayRef(int arraySize, int arrayType, uint32_t id) {
    auto ptr = alloc(sizeof(ArrayValueRef) + (arraySize > 0 ? arraySize - 1 : 0) * sizeof(Value), id);
	if (ptr == nullptr) {
		return Value(0, VALUE_TYPE_NULL);
	}
    ArrayValueRef *arrayRef = new (ptr) ArrayValueRef;
    arrayRef->arrayValue.arraySize = arraySize;
    arrayRef->arrayValue.arrayType = arrayType;
    for (int i = 1; i < arraySize; i++) {
        new (arrayRef->arrayValue.values + i) Value();
    }
    arrayRef->refCounter = 1;
    Value value;
    value.type = VALUE_TYPE_ARRAY_REF;
    value.options = VALUE_OPTIONS_REF;
    value.refValue = arrayRef;
	return value;
}
Value Value::makeArrayElementRef(Value arrayValue, int elementIndex, uint32_t id) {
    auto arrayElementValueRef = ObjectAllocator<ArrayElementValue>::allocate(id);
	if (arrayElementValueRef == nullptr) {
		return Value(0, VALUE_TYPE_NULL);
	}
    arrayElementValueRef->arrayValue = arrayValue;
    arrayElementValueRef->elementIndex = elementIndex;
    arrayElementValueRef->refCounter = 1;
    Value value;
    value.type = VALUE_TYPE_ARRAY_ELEMENT_VALUE;
    value.options = VALUE_OPTIONS_REF;
    value.refValue = arrayElementValueRef;
	return value;
}
Value Value::makeJsonMemberRef(Value jsonValue, Value propertyName, uint32_t id) {
    auto jsonMemberValueRef = ObjectAllocator<JsonMemberValue>::allocate(id);
	if (jsonMemberValueRef == nullptr) {
		return Value(0, VALUE_TYPE_NULL);
	}
    jsonMemberValueRef->jsonValue = jsonValue;
    jsonMemberValueRef->propertyName = propertyName;
    jsonMemberValueRef->refCounter = 1;
    Value value;
    value.type = VALUE_TYPE_JSON_MEMBER_VALUE;
    value.options = VALUE_OPTIONS_REF;
    value.refValue = jsonMemberValueRef;
	return value;
}
Value Value::makeBlobRef(const uint8_t *blob, uint32_t len, uint32_t id) {
    auto blobRef = ObjectAllocator<BlobRef>::allocate(id);
	if (blobRef == nullptr) {
		return Value(0, VALUE_TYPE_NULL);
	}
	blobRef->blob = (uint8_t *)alloc(len, id + 1);
    if (blobRef->blob == nullptr) {
        ObjectAllocator<BlobRef>::deallocate(blobRef);
        return Value(0, VALUE_TYPE_NULL);
    }
    blobRef->len = len;
    if (blob) {
        memcpy(blobRef->blob, blob, len);
    } else {
        memset(blobRef->blob, 0, len);
    }
    blobRef->refCounter = 1;
    Value value;
    value.type = VALUE_TYPE_BLOB_REF;
    value.options = VALUE_OPTIONS_REF;
    value.refValue = blobRef;
	return value;
}
Value Value::makeBlobRef(const uint8_t *blob1, uint32_t len1, const uint8_t *blob2, uint32_t len2, uint32_t id) {
    auto blobRef = ObjectAllocator<BlobRef>::allocate(id);
	if (blobRef == nullptr) {
		return Value(0, VALUE_TYPE_NULL);
	}
	blobRef->blob = (uint8_t *)alloc(len1 + len2, id + 1);
    if (blobRef->blob == nullptr) {
        ObjectAllocator<BlobRef>::deallocate(blobRef);
        return Value(0, VALUE_TYPE_NULL);
    }
    blobRef->len = len1 + len2;
    memcpy(blobRef->blob, blob1, len1);
    memcpy(blobRef->blob + len1, blob2, len2);
    blobRef->refCounter = 1;
    Value value;
    value.type = VALUE_TYPE_BLOB_REF;
    value.options = VALUE_OPTIONS_REF;
    value.refValue = blobRef;
	return value;
}
Value Value::clone() {
    if (isArray()) {
        auto array = getArray();
        auto resultArrayValue = makeArrayRef(array->arraySize, array->arrayType, 0x0ea48dcb);
        auto resultArray = resultArrayValue.getArray();
        for (uint32_t elementIndex = 0; elementIndex < array->arraySize; elementIndex++) {
            auto elementValue = array->values[elementIndex].clone();
            if (elementValue.isError()) {
                return elementValue;
            }
            resultArray->values[elementIndex] = elementValue;
        }
        return resultArrayValue;
    }
    return *this;
}
#if defined(EEZ_OPTION_GUI)
#if !EEZ_OPTION_GUI
Value getVar(int16_t id) {
    auto native_var = native_vars[id];
    if (native_var.type == NATIVE_VAR_TYPE_INTEGER) {
        auto get = (int32_t (*)())native_var.get;
        return Value((int)get(), VALUE_TYPE_INT32);
    }
    if (native_var.type == NATIVE_VAR_TYPE_BOOLEAN) {
        auto get = (bool (*)())native_var.get;
        return Value(get(), VALUE_TYPE_BOOLEAN);
    }
    if (native_var.type == NATIVE_VAR_TYPE_FLOAT) {
        auto get = (float (*)())native_var.get;
        return Value(get(), VALUE_TYPE_FLOAT);
    }
    if (native_var.type == NATIVE_VAR_TYPE_DOUBLE) {
        auto get = (double (*)())native_var.get;
        return Value(get(), VALUE_TYPE_DOUBLE);
    }
    if (native_var.type == NATIVE_VAR_TYPE_STRING) {
        auto get = (const char *(*)())native_var.get;
        return Value(get(), VALUE_TYPE_STRING);
    }
    return Value();
}
void setVar(int16_t id, const Value& value) {
    auto native_var = native_vars[id];
    if (native_var.type == NATIVE_VAR_TYPE_INTEGER) {
        auto set = (void (*)(int32_t))native_var.set;
        set(value.getInt32());
    }
    if (native_var.type == NATIVE_VAR_TYPE_BOOLEAN) {
        auto set = (void (*)(bool))native_var.set;
        set(value.getBoolean());
    }
    if (native_var.type == NATIVE_VAR_TYPE_FLOAT) {
        auto set = (void (*)(float))native_var.set;
        set(value.getFloat());
    }
    if (native_var.type == NATIVE_VAR_TYPE_DOUBLE) {
        auto set = (void (*)(double))native_var.set;
        set(value.getDouble());
    }
    if (native_var.type == NATIVE_VAR_TYPE_STRING) {
        auto set = (void (*)(const char *))native_var.set;
        set(value.getString());
    }
}
#endif 
#endif 
} 
// -----------------------------------------------------------------------------
// flow/components.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <math.h>
#if defined(EEZ_DASHBOARD_API)
#endif
namespace eez {
namespace flow {
void executeStartComponent(FlowState *flowState, unsigned componentIndex);
void executeEndComponent(FlowState *flowState, unsigned componentIndex);
void executeInputComponent(FlowState *flowState, unsigned componentIndex);
void executeOutputComponent(FlowState *flowState, unsigned componentIndex);
void executeWatchVariableComponent(FlowState *flowState, unsigned componentIndex);
void executeEvalExprComponent(FlowState *flowState, unsigned componentIndex);
void executeSetVariableComponent(FlowState *flowState, unsigned componentIndex);
void executeSwitchComponent(FlowState *flowState, unsigned componentIndex);
void executeCompareComponent(FlowState *flowState, unsigned componentIndex);
void executeIsTrueComponent(FlowState *flowState, unsigned componentIndex);
void executeConstantComponent(FlowState *flowState, unsigned componentIndex);
void executeLogComponent(FlowState *flowState, unsigned componentIndex);
void executeCallActionComponent(FlowState *flowState, unsigned componentIndex);
void executeDelayComponent(FlowState *flowState, unsigned componentIndex);
void executeErrorComponent(FlowState *flowState, unsigned componentIndex);
void executeCatchErrorComponent(FlowState *flowState, unsigned componentIndex);
void executeCounterComponent(FlowState *flowState, unsigned componentIndex);
void executeLoopComponent(FlowState *flowState, unsigned componentIndex);
void executeShowPageComponent(FlowState *flowState, unsigned componentIndex);
#if EEZ_OPTION_GUI
void executeShowMessageBoxComponent(FlowState *flowState, unsigned componentIndex);
void executeShowKeyboardComponent(FlowState *flowState, unsigned componentIndex);
void executeShowKeypadComponent(FlowState *flowState, unsigned componentIndex);
void executeSetPageDirectionComponent(FlowState *flowState, unsigned componentIndex);
void executeOverrideStyleComponent(FlowState *flowState, unsigned componentIndex);
#endif
void executeSelectLanguageComponent(FlowState *flowState, unsigned componentIndex);
void executeAnimateComponent(FlowState *flowState, unsigned componentIndex);
void executeNoopComponent(FlowState *flowState, unsigned componentIndex);
void executeOnEventComponent(FlowState *flowState, unsigned componentIndex);
void executeLVGLComponent(FlowState *flowState, unsigned componentIndex);
void executeLVGLUserWidgetComponent(FlowState *flowState, unsigned componentIndex);
void executeSortArrayComponent(FlowState *flowState, unsigned componentIndex);
void executeTestAndSetComponent(FlowState *flowState, unsigned componentIndex);
#if EEZ_OPTION_GUI
void executeUserWidgetWidgetComponent(FlowState *flowState, unsigned componentIndex);
void executeLineChartWidgetComponent(FlowState *flowState, unsigned componentIndex);
void executeRollerWidgetComponent(FlowState *flowState, unsigned componentIndex);
#endif
void executeMQTTInitComponent(FlowState *flowState, unsigned componentIndex);
void executeMQTTConnectComponent(FlowState *flowState, unsigned componentIndex);
void executeMQTTDisconnectComponent(FlowState *flowState, unsigned componentIndex);
void executeMQTTEventComponent(FlowState *flowState, unsigned componentIndex);
void executeMQTTSubscribeComponent(FlowState *flowState, unsigned componentIndex);
void executeMQTTUnsubscribeComponent(FlowState *flowState, unsigned componentIndex);
void executeMQTTPublishComponent(FlowState *flowState, unsigned componentIndex);
void executeLabelInComponent(FlowState *flowState, unsigned componentIndex);
void executeLabelOutComponent(FlowState *flowState, unsigned componentIndex);
typedef void (*ExecuteComponentFunctionType)(FlowState *flowState, unsigned componentIndex);
static ExecuteComponentFunctionType g_executeComponentFunctions[] = {
	executeStartComponent,
	executeEndComponent,
	executeInputComponent,
	executeOutputComponent,
	executeWatchVariableComponent,
	executeEvalExprComponent,
	executeSetVariableComponent,
	executeSwitchComponent,
	executeCompareComponent,
	executeIsTrueComponent,
	executeConstantComponent,
	executeLogComponent,
	executeCallActionComponent,
	executeDelayComponent,
	executeErrorComponent,
	executeCatchErrorComponent,
	executeCounterComponent, 
	executeLoopComponent,
	executeShowPageComponent,
	nullptr, 
#if EEZ_OPTION_GUI
	executeShowMessageBoxComponent,
	executeShowKeyboardComponent,
	executeShowKeypadComponent,
#else
    nullptr,
    nullptr,
    nullptr,
#endif
	executeNoopComponent, 
	nullptr, 
    executeSelectLanguageComponent, 
#if EEZ_OPTION_GUI
    executeSetPageDirectionComponent, 
#else
    nullptr,
#endif
    executeAnimateComponent, 
    executeOnEventComponent, 
    executeLVGLComponent, 
#if EEZ_OPTION_GUI
    executeOverrideStyleComponent, 
#else
    nullptr,
#endif
    executeSortArrayComponent, 
    executeLVGLUserWidgetComponent, 
    executeTestAndSetComponent, 
    executeMQTTInitComponent, 
    executeMQTTConnectComponent, 
    executeMQTTDisconnectComponent, 
    executeMQTTEventComponent, 
    executeMQTTSubscribeComponent, 
    executeMQTTUnsubscribeComponent, 
    executeMQTTPublishComponent, 
    executeLabelInComponent, 
    executeLabelOutComponent, 
};
void registerComponent(ComponentTypes componentType, ExecuteComponentFunctionType executeComponentFunction) {
	if (componentType >= defs_v3::COMPONENT_TYPE_START_ACTION) {
		g_executeComponentFunctions[componentType - defs_v3::COMPONENT_TYPE_START_ACTION] = executeComponentFunction;
	}
}
void executeComponent(FlowState *flowState, unsigned componentIndex) {
	auto component = flowState->flow->components[componentIndex];
	if (component->type >= defs_v3::FIRST_DASHBOARD_ACTION_COMPONENT_TYPE) {
#if defined(EEZ_DASHBOARD_API)
        executeDashboardComponent(component->type, getFlowStateIndex(flowState), componentIndex);
#endif 
        return;
    } else if (component->type >= defs_v3::COMPONENT_TYPE_START_ACTION) {
		auto executeComponentFunction = g_executeComponentFunctions[component->type - defs_v3::COMPONENT_TYPE_START_ACTION];
		if (executeComponentFunction != nullptr) {
			executeComponentFunction(flowState, componentIndex);
			return;
		}
	}
#if EEZ_OPTION_GUI
    else if (component->type < 1000) {
		if (component->type == defs_v3::COMPONENT_TYPE_USER_WIDGET_WIDGET) {
            executeUserWidgetWidgetComponent(flowState, componentIndex);
        } else if (component->type == defs_v3::COMPONENT_TYPE_LINE_CHART_EMBEDDED_WIDGET) {
			executeLineChartWidgetComponent(flowState, componentIndex);
		} else if (component->type == defs_v3::COMPONENT_TYPE_ROLLER_WIDGET) {
			executeRollerWidgetComponent(flowState, componentIndex);
		}
		return;
	}
#endif
	char errorMessage[100];
	snprintf(errorMessage, sizeof(errorMessage), "Unknown component at index = %d, type = %d\n", componentIndex, component->type);
	throwError(flowState, componentIndex, errorMessage);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/date.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
namespace eez {
namespace flow {
namespace date {
#define SECONDS_PER_MINUTE 60UL
#define SECONDS_PER_HOUR (SECONDS_PER_MINUTE * 60)
#define SECONDS_PER_DAY (SECONDS_PER_HOUR * 24)
#define LEAP_YEAR(Y)                                                                               \
    (((1970 + Y) > 0) && !((1970 + Y) % 4) && (((1970 + Y) % 100) || !((1970 + Y) % 400)))
static const uint8_t monthDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
enum Week { Last, First, Second, Third, Fourth };
enum DayOfWeek { Sun = 1, Mon, Tue, Wed, Thu, Fri, Sat };
enum Month { Jan = 1, Feb, Mar, Apr, May, Jun, Jul, Aug, Sep, Oct, Nov, Dec };
struct TimeChangeRule {
    Week week;
    DayOfWeek dow;
    uint8_t month;
    uint8_t hours;
};
static struct {
    TimeChangeRule dstStart;
    TimeChangeRule dstEnd;
} g_dstRules[] = {
    { { Last, Sun, Mar, 2 }, { Last, Sun, Oct, 3 } },    
    { { Second, Sun, Mar, 2 }, { First, Sun, Nov, 2 } }, 
    { { First, Sun, Oct, 2 }, { First, Sun, Apr, 3 } },  
};
Format g_localeFormat = FORMAT_DMY_24;
int g_timeZone = 0;
DstRule g_dstRule = DST_RULE_OFF;
static void convertTime24to12(int &hours, bool &am);
static bool isDst(Date time, DstRule dstRule);
static uint8_t dayOfWeek(int y, int m, int d);
static Date timeChangeRuleToLocal(TimeChangeRule &r, int year);
Date now() {
    return utcToLocal(getDateNowHook());
}
void toString(Date time, char *str, uint32_t strLen) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    snprintf(str, strLen, "%04d-%02d-%02dT%02d:%02d:%02d.%06d", year, month, day, hours, minutes, seconds, milliseconds);
}
void toLocaleString(Date time, char *str, uint32_t strLen) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    if (g_localeFormat == FORMAT_DMY_24) {
        snprintf(str, strLen, "%02d-%02d-%02d %02d:%02d:%02d.%03d", day, month, year, hours, minutes, seconds, milliseconds);
    } else if (g_localeFormat == FORMAT_MDY_24) {
        snprintf(str, strLen, "%02d-%02d-%02d %02d:%02d:%02d.%03d", month, day, year, hours, minutes, seconds, milliseconds);
    } else if (g_localeFormat == FORMAT_DMY_12) {
        bool am;
        convertTime24to12(hours, am);
        snprintf(str, strLen, "%02d-%02d-%02d %02d:%02d:%02d.%03d %s", day, month, year, hours, minutes, seconds, milliseconds, am ? "AM" : "PM");
    } else if (g_localeFormat == FORMAT_MDY_12) {
        bool am;
        convertTime24to12(hours, am);
        snprintf(str, strLen, "%02d-%02d-%02d %02d:%02d:%02d.%03d %s", month, day, year, hours, minutes, seconds, milliseconds, am ? "AM" : "PM");
    }
}
Date fromString(const char *str) {
    int year = 0, month = 0, day = 0, hours = 0, minutes = 0, seconds = 0, milliseconds = 0;
    sscanf(str, "%d-%d-%dT%d:%d:%d.%d", &year, &month, &day, &hours, &minutes, &seconds, &milliseconds);
    return makeDate(year, month, day, hours, minutes, seconds, milliseconds);
}
Date makeDate(int year, int month, int day, int hours, int minutes, int seconds, int milliseconds) {
    year -= 1970;
    Date time = year * 365 * SECONDS_PER_DAY;
    for (int i = 0; i < year; i++) {
        if (LEAP_YEAR(i)) {
            time += SECONDS_PER_DAY; 
        }
    }
    for (int i = 1; i < month; i++) {
        if ((i == 2) && LEAP_YEAR(year)) {
            time += SECONDS_PER_DAY * 29;
        } else {
            time += SECONDS_PER_DAY * monthDays[i - 1]; 
        }
    }
    time += (day - 1) * SECONDS_PER_DAY;
    time += hours * SECONDS_PER_HOUR;
    time += minutes * SECONDS_PER_MINUTE;
    time += seconds;
    time *= 1000;
    time += milliseconds;
    return time;
}
void breakDate(Date time, int &result_year, int &result_month, int &result_day, int &result_hours, int &result_minutes, int &result_seconds, int &result_milliseconds) {
    uint8_t year;
    uint8_t month, monthLength;
    uint32_t days;
    result_milliseconds = time % 1000;
    time /= 1000; 
    result_seconds = time % 60;
    time /= 60; 
    result_minutes = time % 60;
    time /= 60; 
    result_hours = time % 24;
    time /= 24; 
    year = 0;
    days = 0;
    while ((unsigned)(days += (LEAP_YEAR(year) ? 366 : 365)) <= time) {
        year++;
    }
    result_year = year + 1970; 
    days -= LEAP_YEAR(year) ? 366 : 365;
    time -= days; 
    days = 0;
    month = 0;
    monthLength = 0;
    for (month = 0; month < 12; ++month) {
        if (month == 1) { 
            if (LEAP_YEAR(year)) {
                monthLength = 29;
            } else {
                monthLength = 28;
            }
        } else {
            monthLength = monthDays[month];
        }
        if (time >= monthLength) {
            time -= monthLength;
        } else {
            break;
        }
    }
    result_month = month + 1; 
    result_day = time + 1;    
}
int getYear(Date time) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    return year;
}
int getMonth(Date time) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    return month;
}
int getDay(Date time) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    return day;
}
int getHours(Date time) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    return hours;
}
int getMinutes(Date time) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    return minutes;
}
int getSeconds(Date time) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    return seconds;
}
int getMilliseconds(Date time) {
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(time, year, month, day, hours, minutes, seconds, milliseconds);
    return milliseconds;
}
Date utcToLocal(Date utc) {
    Date local = utc + ((g_timeZone / 100) * 60 + g_timeZone % 100) * 60L * 1000L;
    if (isDst(local, g_dstRule)) {
        local += SECONDS_PER_HOUR * 1000L;
    }
    return local;
}
Date localToUtc(Date local) {
    Date utc = local - ((g_timeZone / 100) * 60 + g_timeZone % 100) * 60L * 1000L;
    if (isDst(local, g_dstRule)) {
        utc -= SECONDS_PER_HOUR * 1000L;
    }
    return utc;
}
static void convertTime24to12(int &hours, bool &am) {
    if (hours == 0) {
        hours = 12;
        am = true;
    } else if (hours < 12) {
        am = true;
    } else if (hours == 12) {
        am = false;
    } else {
        hours = hours - 12;
        am = false;
    }
}
static bool isDst(Date local, DstRule dstRule) {
    if (dstRule == DST_RULE_OFF) {
        return false;
    }
    int year, month, day, hours, minutes, seconds, milliseconds;
    breakDate(local, year, month, day, hours, minutes, seconds, milliseconds);
    Date dstStart = timeChangeRuleToLocal(g_dstRules[dstRule - 1].dstStart, year);
    Date dstEnd = timeChangeRuleToLocal(g_dstRules[dstRule - 1].dstEnd, year);
    return (dstStart < dstEnd && (local >= dstStart && local < dstEnd)) ||
           (dstStart > dstEnd && (local >= dstStart || local < dstEnd));
}
static uint8_t dayOfWeek(int y, int m, int d) {
    static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) {
        --y;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7 + 1;
}
static Date timeChangeRuleToLocal(TimeChangeRule &r, int year) {
    uint8_t month = r.month;
    uint8_t week = r.week;
    if (week == 0) {        
        if (++month > 12) { 
            month = 1;
            ++year;
        }
        week = 1; 
    }
    Date time = makeDate(year, month, 1, r.hours, 0, 0, 0);
    uint8_t dow = dayOfWeek(year, month, 1);
    time += (7 * (week - 1) + (r.dow - dow + 7) % 7) * SECONDS_PER_DAY;
    if (r.week == 0) {
        time -= 7 * SECONDS_PER_DAY; 
    }
    return time;
}
} 
} 
} 
// -----------------------------------------------------------------------------
// flow/debugger.cpp
// -----------------------------------------------------------------------------
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
namespace eez {
namespace flow {
enum MessagesToDebugger {
    MESSAGE_TO_DEBUGGER_STATE_CHANGED, 
    MESSAGE_TO_DEBUGGER_ADD_TO_QUEUE, 
    MESSAGE_TO_DEBUGGER_REMOVE_FROM_QUEUE, 
    MESSAGE_TO_DEBUGGER_GLOBAL_VARIABLE_INIT, 
    MESSAGE_TO_DEBUGGER_LOCAL_VARIABLE_INIT, 
    MESSAGE_TO_DEBUGGER_COMPONENT_INPUT_INIT, 
    MESSAGE_TO_DEBUGGER_VALUE_CHANGED, 
    MESSAGE_TO_DEBUGGER_FLOW_STATE_CREATED, 
    MESSAGE_TO_DEBUGGER_FLOW_STATE_TIMELINE_CHANGED, 
    MESSAGE_TO_DEBUGGER_FLOW_STATE_DESTROYED, 
	MESSAGE_TO_DEBUGGER_FLOW_STATE_ERROR, 
    MESSAGE_TO_DEBUGGER_LOG, 
	MESSAGE_TO_DEBUGGER_PAGE_CHANGED, 
    MESSAGE_TO_DEBUGGER_COMPONENT_EXECUTION_STATE_CHANGED, 
    MESSAGE_TO_DEBUGGER_COMPONENT_ASYNC_STATE_CHANGED 
};
enum MessagesFromDebugger {
    MESSAGE_FROM_DEBUGGER_RESUME, 
    MESSAGE_FROM_DEBUGGER_PAUSE, 
    MESSAGE_FROM_DEBUGGER_SINGLE_STEP, 
    MESSAGE_FROM_DEBUGGER_ADD_BREAKPOINT, 
    MESSAGE_FROM_DEBUGGER_REMOVE_BREAKPOINT, 
    MESSAGE_FROM_DEBUGGER_ENABLE_BREAKPOINT, 
    MESSAGE_FROM_DEBUGGER_DISABLE_BREAKPOINT, 
    MESSAGE_FROM_DEBUGGER_MODE 
};
enum LogItemType {
	LOG_ITEM_TYPE_FATAL,
	LOG_ITEM_TYPE_ERROR,
    LOG_ITEM_TYPE_WARNING ,
    LOG_ITEM_TYPE_SCPI,
    LOG_ITEM_TYPE_INFO,
    LOG_ITEM_TYPE_DEBUG
};
enum DebuggerState {
    DEBUGGER_STATE_RESUMED,
    DEBUGGER_STATE_PAUSED,
    DEBUGGER_STATE_SINGLE_STEP,
    DEBUGGER_STATE_STOPPED,
};
bool g_debuggerIsConnected;
static uint32_t g_messageSubsciptionFilter = 0xFFFFFFFF;
static DebuggerState g_debuggerState;
static bool g_skipNextBreakpoint;
static char g_inputFromDebugger[64];
static unsigned g_inputFromDebuggerPosition;
int g_debuggerMode = DEBUGGER_MODE_RUN;
void setDebuggerMessageSubsciptionFilter(uint32_t filter) {
    g_messageSubsciptionFilter = filter;
}
bool isSubscribedTo(MessagesToDebugger messageType) {
    if (g_debuggerIsConnected && (g_messageSubsciptionFilter & (1 << messageType)) != 0) {
        startToDebuggerMessageHook();
        return true;
    }
    return false;
}
static void setDebuggerState(DebuggerState newState) {
	if (newState != g_debuggerState) {
		g_debuggerState = newState;
		if (isSubscribedTo(MESSAGE_TO_DEBUGGER_STATE_CHANGED)) {
			char buffer[256];
			snprintf(buffer, sizeof(buffer), "%d\t%d\n",
				MESSAGE_TO_DEBUGGER_STATE_CHANGED,
				g_debuggerState
			);
			writeDebuggerBufferHook(buffer, strlen(buffer));
		}
	}
}
void onDebuggerClientConnected() {
    g_debuggerIsConnected = true;
	g_skipNextBreakpoint = false;
	g_inputFromDebuggerPosition = 0;
    setDebuggerState(DEBUGGER_STATE_PAUSED);
}
void onDebuggerClientDisconnected() {
    g_debuggerIsConnected = false;
    setDebuggerState(DEBUGGER_STATE_RESUMED);
}
void processDebuggerInput(char *buffer, uint32_t length) {
	for (uint32_t i = 0; i < length; i++) {
		if (buffer[i] == '\n') {
			int messageFromDebugger = g_inputFromDebugger[0] - '0';
			if (messageFromDebugger == MESSAGE_FROM_DEBUGGER_RESUME) {
				setDebuggerState(DEBUGGER_STATE_RESUMED);
			} else if (messageFromDebugger == MESSAGE_FROM_DEBUGGER_PAUSE) {
				setDebuggerState(DEBUGGER_STATE_PAUSED);
			} else if (messageFromDebugger == MESSAGE_FROM_DEBUGGER_SINGLE_STEP) {
				setDebuggerState(DEBUGGER_STATE_SINGLE_STEP);
			} else if (
				messageFromDebugger >= MESSAGE_FROM_DEBUGGER_ADD_BREAKPOINT &&
				messageFromDebugger <= MESSAGE_FROM_DEBUGGER_DISABLE_BREAKPOINT
			) {
				char *p;
				auto flowIndex = (uint32_t)strtol(g_inputFromDebugger + 2, &p, 10);
				auto componentIndex = (uint32_t)strtol(p + 1, nullptr, 10);
				auto assets = g_firstFlowState->assets;
				auto flowDefinition = static_cast<FlowDefinition *>(assets->flowDefinition);
				if (flowIndex >= 0 && flowIndex < flowDefinition->flows.count) {
					auto flow = flowDefinition->flows[flowIndex];
					if (componentIndex >= 0 && componentIndex < flow->components.count) {
						auto component = flow->components[componentIndex];
						component->breakpoint = messageFromDebugger == MESSAGE_FROM_DEBUGGER_ADD_BREAKPOINT ||
							messageFromDebugger == MESSAGE_FROM_DEBUGGER_ENABLE_BREAKPOINT ? 1 : 0;
					} else {
						ErrorTrace("Invalid breakpoint component index\n");
					}
				} else {
					ErrorTrace("Invalid breakpoint flow index\n");
				}
			} else if (messageFromDebugger == MESSAGE_FROM_DEBUGGER_MODE) {
                g_debuggerMode = strtol(g_inputFromDebugger + 2, nullptr, 10);
#if EEZ_OPTION_GUI
                gui::refreshScreen();
#endif
            }
			g_inputFromDebuggerPosition = 0;
		} else {
			if (g_inputFromDebuggerPosition < sizeof(g_inputFromDebugger)) {
				g_inputFromDebugger[g_inputFromDebuggerPosition++] = buffer[i];
			} else if (g_inputFromDebuggerPosition == sizeof(g_inputFromDebugger)) {
				ErrorTrace("Input from debugger buffer overflow\n");
			}
		}
	}
}
bool canExecuteStep(FlowState *&flowState, unsigned &componentIndex) {
    if (!g_debuggerIsConnected) {
        return true;
    }
    if (!isSubscribedTo(MESSAGE_TO_DEBUGGER_ADD_TO_QUEUE)) {
        return true;
    }
    if (g_debuggerState == DEBUGGER_STATE_PAUSED) {
        return false;
    }
    if (g_debuggerState == DEBUGGER_STATE_SINGLE_STEP) {
        g_skipNextBreakpoint = false;
	    setDebuggerState(DEBUGGER_STATE_PAUSED);
        return true;
    }
    if (g_skipNextBreakpoint) {
        g_skipNextBreakpoint = false;
    } else {
        auto component = flowState->flow->components[componentIndex];
        if (component->breakpoint) {
            g_skipNextBreakpoint = true;
			setDebuggerState(DEBUGGER_STATE_PAUSED);
            return false;
        }
    }
    return true;
}
#if defined(__EMSCRIPTEN__)
char outputBuffer[1024 * 1024];
#else
char outputBuffer[64];
#endif
int outputBufferPosition = 0;
#define WRITE_TO_OUTPUT_BUFFER(ch) \
	outputBuffer[outputBufferPosition++] = ch; \
	if (outputBufferPosition == sizeof(outputBuffer)) { \
		writeDebuggerBufferHook(outputBuffer, outputBufferPosition); \
		outputBufferPosition = 0; \
	}
#define FLUSH_OUTPUT_BUFFER() \
	if (outputBufferPosition > 0) { \
		writeDebuggerBufferHook(outputBuffer, outputBufferPosition); \
		outputBufferPosition = 0; \
	}
void writeValueAddr(const void *pValue) {
	char tmpStr[32];
	snprintf(tmpStr, sizeof(tmpStr), "%p", pValue);
	auto len = strlen(tmpStr);
	for (size_t i = 0; i < len; i++) {
		WRITE_TO_OUTPUT_BUFFER(tmpStr[i]);
	}
}
void writeString(const char *str) {
	WRITE_TO_OUTPUT_BUFFER('"');
    while (true) {
        utf8_int32_t cp;
        str = utf8codepoint(str, &cp);
        if (!cp) {
            break;
        }
        if (cp == '"') {
			WRITE_TO_OUTPUT_BUFFER('\\');
			WRITE_TO_OUTPUT_BUFFER('"');
		} else if (cp == '\t') {
			WRITE_TO_OUTPUT_BUFFER('\\');
			WRITE_TO_OUTPUT_BUFFER('t');
		} else if (cp == '\n') {
			WRITE_TO_OUTPUT_BUFFER('\\');
			WRITE_TO_OUTPUT_BUFFER('n');
		} else if (cp >= 32 && cp < 127) {
			WRITE_TO_OUTPUT_BUFFER(cp);
        } else {
            char temp[32];
            snprintf(temp, sizeof(temp), "\\u%04x", (int)cp);
            for (size_t i = 0; i < strlen(temp); i++) {
			    WRITE_TO_OUTPUT_BUFFER(temp[i]);
            }
        }
    }
	WRITE_TO_OUTPUT_BUFFER('"');
	WRITE_TO_OUTPUT_BUFFER('\n');
	FLUSH_OUTPUT_BUFFER();
}
void writeArrayType(uint32_t arrayType) {
	char tmpStr[32];
	snprintf(tmpStr, sizeof(tmpStr), "%x", (int)arrayType);
	auto len = strlen(tmpStr);
	for (size_t i = 0; i < len; i++) {
		WRITE_TO_OUTPUT_BUFFER(tmpStr[i]);
	}
}
void writeArray(const ArrayValue *arrayValue) {
	WRITE_TO_OUTPUT_BUFFER('{');
	writeValueAddr(arrayValue);
    WRITE_TO_OUTPUT_BUFFER(',');
    writeArrayType(arrayValue->arrayType);
	for (uint32_t i = 0; i < arrayValue->arraySize; i++) {
		WRITE_TO_OUTPUT_BUFFER(',');
		writeValueAddr(&arrayValue->values[i]);
	}
	WRITE_TO_OUTPUT_BUFFER('}');
	WRITE_TO_OUTPUT_BUFFER('\n');
	FLUSH_OUTPUT_BUFFER();
    for (uint32_t i = 0; i < arrayValue->arraySize; i++) {
        onValueChanged(&arrayValue->values[i]);
    }
}
void writeHex(char *dst, uint8_t *src, size_t srcLength) {
    *dst++ = 'H';
    for (size_t i = 0; i < srcLength; i++) {
        *dst++ = toHexDigit(src[i] / 16);
        *dst++ = toHexDigit(src[i] % 16);
    }
    *dst++ = 0;
}
void writeValue(const Value &value) {
	char tempStr[64];
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4474)
#endif
	switch (value.getType()) {
	case VALUE_TYPE_UNDEFINED:
		stringCopy(tempStr, sizeof(tempStr) - 1, "undefined");
		break;
	case VALUE_TYPE_NULL:
		stringCopy(tempStr, sizeof(tempStr) - 1, "null");
		break;
	case VALUE_TYPE_BOOLEAN:
		stringCopy(tempStr, sizeof(tempStr) - 1, value.getBoolean() ? "true" : "false");
		break;
	case VALUE_TYPE_INT8:
		snprintf(tempStr, sizeof(tempStr) - 1, "%" PRId8 "", value.int8Value);
		break;
	case VALUE_TYPE_UINT8:
		snprintf(tempStr, sizeof(tempStr) - 1, "%" PRIu8 "", value.uint8Value);
		break;
	case VALUE_TYPE_INT16:
		snprintf(tempStr, sizeof(tempStr) - 1, "%" PRId16 "", value.int16Value);
		break;
	case VALUE_TYPE_UINT16:
		snprintf(tempStr, sizeof(tempStr) - 1, "%" PRIu16 "", value.uint16Value);
		break;
	case VALUE_TYPE_INT32:
		snprintf(tempStr, sizeof(tempStr) - 1, "%" PRId32 "", value.int32Value);
		break;
	case VALUE_TYPE_UINT32:
		snprintf(tempStr, sizeof(tempStr) - 1, "%" PRIu32 "", value.uint32Value);
		break;
	case VALUE_TYPE_INT64:
		snprintf(tempStr, sizeof(tempStr) - 1, "%" PRId64 "", value.int64Value);
		break;
	case VALUE_TYPE_UINT64:
		snprintf(tempStr, sizeof(tempStr) - 1, "%" PRIu64 "", value.uint64Value);
		break;
	case VALUE_TYPE_DOUBLE:
        writeHex(tempStr, (uint8_t *)&value.doubleValue, sizeof(double));
		break;
	case VALUE_TYPE_FLOAT:
        writeHex(tempStr, (uint8_t *)&value.floatValue, sizeof(float));
		break;
	case VALUE_TYPE_STRING:
    case VALUE_TYPE_STRING_ASSET:
	case VALUE_TYPE_STRING_REF:
		writeString(value.getString());
		return;
	case VALUE_TYPE_ARRAY:
    case VALUE_TYPE_ARRAY_ASSET:
	case VALUE_TYPE_ARRAY_REF:
		writeArray(value.getArray());
		return;
	case VALUE_TYPE_BLOB_REF:
		snprintf(tempStr, sizeof(tempStr) - 1, "@%d", (int)((BlobRef *)value.refValue)->len);
		break;
	case VALUE_TYPE_STREAM:
		snprintf(tempStr, sizeof(tempStr) - 1, ">%d", (int)(value.int32Value));
		break;
	case VALUE_TYPE_JSON:
		snprintf(tempStr, sizeof(tempStr) - 1, "#%d", (int)(value.int32Value));
		break;
	case VALUE_TYPE_DATE:
        tempStr[0] = '!';
		writeHex(tempStr + 1, (uint8_t *)&value.doubleValue, sizeof(double));
		break;
    case VALUE_TYPE_POINTER:
        snprintf(tempStr, sizeof(tempStr) - 1, "%" PRIu64 "", (uint64_t)value.getVoidPointer());
		break;
	default:
		tempStr[0] = 0;
		break;
	}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
	stringAppendString(tempStr, sizeof(tempStr), "\n");
	writeDebuggerBufferHook(tempStr, strlen(tempStr));
}
void onStarted(Assets *assets) {
    if (isSubscribedTo(MESSAGE_TO_DEBUGGER_GLOBAL_VARIABLE_INIT)) {
		auto flowDefinition = static_cast<FlowDefinition *>(assets->flowDefinition);
        if (g_globalVariables) {
            for (uint32_t i = 0; i < g_globalVariables->count; i++) {
                auto pValue = g_globalVariables->values + i;
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "%d\t%d\t%p\t",
                    MESSAGE_TO_DEBUGGER_GLOBAL_VARIABLE_INIT,
                    (int)i,
                    (const void *)pValue
                );
                writeDebuggerBufferHook(buffer, strlen(buffer));
                writeValue(*pValue);
            }
        } else {
            for (uint32_t i = 0; i < flowDefinition->globalVariables.count; i++) {
                auto pValue = flowDefinition->globalVariables[i];
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "%d\t%d\t%p\t",
                    MESSAGE_TO_DEBUGGER_GLOBAL_VARIABLE_INIT,
                    (int)i,
                    (const void *)pValue
                );
                writeDebuggerBufferHook(buffer, strlen(buffer));
                writeValue(*pValue);
            }
        }
    }
}
void onStopped() {
    setDebuggerState(DEBUGGER_STATE_STOPPED);
}
void onAddToQueue(FlowState *flowState, int sourceComponentIndex, int sourceOutputIndex, unsigned targetComponentIndex, int targetInputIndex) {
    if (isSubscribedTo(MESSAGE_TO_DEBUGGER_ADD_TO_QUEUE)) {
        uint32_t free;
        uint32_t alloc;
        getAllocInfo(free, alloc);
        char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
			MESSAGE_TO_DEBUGGER_ADD_TO_QUEUE,
			(int)flowState->flowStateIndex,
			sourceComponentIndex,
			sourceOutputIndex,
			targetComponentIndex,
			targetInputIndex,
            (int)free,
            (int)ALLOC_BUFFER_SIZE
		);
        writeDebuggerBufferHook(buffer, strlen(buffer));
    }
}
void onRemoveFromQueue() {
    if (isSubscribedTo(MESSAGE_TO_DEBUGGER_REMOVE_FROM_QUEUE)) {
        char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\n",
			MESSAGE_TO_DEBUGGER_REMOVE_FROM_QUEUE
		);
        writeDebuggerBufferHook(buffer, strlen(buffer));
    }
}
void onValueChanged(const Value *pValue) {
    if (isSubscribedTo(MESSAGE_TO_DEBUGGER_VALUE_CHANGED)) {
        char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%p\t",
			MESSAGE_TO_DEBUGGER_VALUE_CHANGED,
            (const void *)pValue
		);
        writeDebuggerBufferHook(buffer, strlen(buffer));
		writeValue(*pValue);
    }
}
void onFlowStateCreated(FlowState *flowState) {
    if (isSubscribedTo(MESSAGE_TO_DEBUGGER_FLOW_STATE_CREATED)) {
        char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%d\t%d\n",
			MESSAGE_TO_DEBUGGER_FLOW_STATE_CREATED,
			(int)flowState->flowStateIndex,
			(int)flowState->flowIndex,
			(int)(flowState->parentFlowState ? flowState->parentFlowState->flowStateIndex : -1),
			(int)flowState->parentComponentIndex
		);
        writeDebuggerBufferHook(buffer, strlen(buffer));
    }
    if (isSubscribedTo(MESSAGE_TO_DEBUGGER_LOCAL_VARIABLE_INIT)) {
		auto flow = flowState->flow;
		for (uint32_t i = 0; i < flow->localVariables.count; i++) {
			auto pValue = &flowState->values[flow->componentInputs.count + i];
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%p\t",
                MESSAGE_TO_DEBUGGER_LOCAL_VARIABLE_INIT,
				(int)flowState->flowStateIndex,
				(int)i,
                (const void *)pValue
            );
            writeDebuggerBufferHook(buffer, strlen(buffer));
			writeValue(*pValue);
        }
    }
    if (isSubscribedTo(MESSAGE_TO_DEBUGGER_COMPONENT_INPUT_INIT)) {
		auto flow = flowState->flow;
		for (uint32_t i = 0; i < flow->componentInputs.count; i++) {
				auto pValue = &flowState->values[i];
				char buffer[256];
				snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%p\t",
					MESSAGE_TO_DEBUGGER_COMPONENT_INPUT_INIT,
					(int)flowState->flowStateIndex,
					(int)i,
					(const void *)pValue
				);
				writeDebuggerBufferHook(buffer, strlen(buffer));
				writeValue(*pValue);
        }
	}
}
void onFlowStateDestroyed(FlowState *flowState) {
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_FLOW_STATE_DESTROYED)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\n",
			MESSAGE_TO_DEBUGGER_FLOW_STATE_DESTROYED,
			(int)flowState->flowStateIndex
		);
		writeDebuggerBufferHook(buffer, strlen(buffer));
	}
}
void onFlowStateTimelineChanged(FlowState *flowState) {
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_FLOW_STATE_TIMELINE_CHANGED)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%g\n",
			MESSAGE_TO_DEBUGGER_FLOW_STATE_TIMELINE_CHANGED,
			(int)flowState->flowStateIndex,
            flowState->timelinePosition
		);
		writeDebuggerBufferHook(buffer, strlen(buffer));
	}
}
void onFlowError(FlowState *flowState, int componentIndex, const char *errorMessage) {
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_FLOW_STATE_ERROR)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t",
			MESSAGE_TO_DEBUGGER_FLOW_STATE_ERROR,
			(int)flowState->flowStateIndex,
			componentIndex
		);
		writeDebuggerBufferHook(buffer, strlen(buffer));
		writeString(errorMessage);
	}
    if (onFlowErrorHook) {
        onFlowErrorHook(flowState, componentIndex, errorMessage);
    }
}
void onComponentExecutionStateChanged(FlowState *flowState, int componentIndex) {
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_COMPONENT_EXECUTION_STATE_CHANGED)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%p\n",
			MESSAGE_TO_DEBUGGER_COMPONENT_EXECUTION_STATE_CHANGED,
			(int)flowState->flowStateIndex,
			componentIndex,
            (void *)flowState->componenentExecutionStates[componentIndex]
		);
        writeDebuggerBufferHook(buffer, strlen(buffer));
	}
}
void onComponentAsyncStateChanged(FlowState *flowState, int componentIndex) {
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_COMPONENT_ASYNC_STATE_CHANGED)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%d\n",
			MESSAGE_TO_DEBUGGER_COMPONENT_ASYNC_STATE_CHANGED,
			(int)flowState->flowStateIndex,
			componentIndex,
            flowState->componenentAsyncStates[componentIndex] ? 1 : 0
		);
        writeDebuggerBufferHook(buffer, strlen(buffer));
	}
}
void writeLogMessage(const char *str) {
	for (const char *p = str; *p; p++) {
		if (*p == '\t') {
			WRITE_TO_OUTPUT_BUFFER('\\');
			WRITE_TO_OUTPUT_BUFFER('t');
		} if (*p == '\n') {
			WRITE_TO_OUTPUT_BUFFER('\\');
			WRITE_TO_OUTPUT_BUFFER('n');
		} else {
			WRITE_TO_OUTPUT_BUFFER(*p);
		}
	}
	WRITE_TO_OUTPUT_BUFFER('\n');
	FLUSH_OUTPUT_BUFFER();
}
void writeLogMessage(const char *str, size_t len) {
	for (size_t i = 0; i < len; i++) {
		if (str[i] == '\t') {
			WRITE_TO_OUTPUT_BUFFER('\\');
			WRITE_TO_OUTPUT_BUFFER('t');
		} if (str[i] == '\n') {
			WRITE_TO_OUTPUT_BUFFER('\\');
			WRITE_TO_OUTPUT_BUFFER('n');
		} else {
			WRITE_TO_OUTPUT_BUFFER(str[i]);
		}
	}
	WRITE_TO_OUTPUT_BUFFER('\n');
	FLUSH_OUTPUT_BUFFER();
}
void logInfo(FlowState *flowState, unsigned componentIndex, const char *message) {
#if defined(EEZ_FOR_LVGL)
    LV_LOG_USER("EEZ-FLOW: %s", message);
#endif
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_LOG)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%d\t",
			MESSAGE_TO_DEBUGGER_LOG,
            LOG_ITEM_TYPE_INFO,
            (int)flowState->flowStateIndex,
			componentIndex
		);
		writeDebuggerBufferHook(buffer, strlen(buffer));
		writeLogMessage(message);
    }
}
void logScpiCommand(FlowState *flowState, unsigned componentIndex, const char *cmd) {
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_LOG)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%d\tSCPI COMMAND: ",
			MESSAGE_TO_DEBUGGER_LOG,
            LOG_ITEM_TYPE_SCPI,
            (int)flowState->flowStateIndex,
			componentIndex
		);
		writeDebuggerBufferHook(buffer, strlen(buffer));
		writeLogMessage(cmd);
    }
}
void logScpiQuery(FlowState *flowState, unsigned componentIndex, const char *query) {
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_LOG)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer), "%d\t%d\t%d\t%d\tSCPI QUERY: ",
			MESSAGE_TO_DEBUGGER_LOG,
            LOG_ITEM_TYPE_SCPI,
            (int)flowState->flowStateIndex,
			componentIndex
		);
		writeDebuggerBufferHook(buffer, strlen(buffer));
		writeLogMessage(query);
    }
}
void logScpiQueryResult(FlowState *flowState, unsigned componentIndex, const char *resultText, size_t resultTextLen) {
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_LOG)) {
		char buffer[256];
		snprintf(buffer, sizeof(buffer) - 1, "%d\t%d\t%d\t%d\tSCPI QUERY RESULT: ",
			MESSAGE_TO_DEBUGGER_LOG,
            LOG_ITEM_TYPE_SCPI,
            (int)flowState->flowStateIndex,
			componentIndex
		);
		writeDebuggerBufferHook(buffer, strlen(buffer));
		writeLogMessage(resultText, resultTextLen);
    }
}
#if EEZ_OPTION_GUI
void onPageChanged(int previousPageId, int activePageId, bool activePageIsFromStack, bool previousPageIsStillOnStack) {
    if (flow::isFlowStopped()) {
        return;
    }
    if (previousPageId == activePageId) {
        return;
    }
    if (!previousPageIsStillOnStack) {
        if (previousPageId > 0 && previousPageId < FIRST_INTERNAL_PAGE_ID) {
            auto flowState = getPageFlowState(g_mainAssets, previousPageId - 1, WidgetCursor());
            if (flowState) {
                onEvent(flowState, FLOW_EVENT_CLOSE_PAGE, Value());
            }
        } else if (previousPageId < 0) {
            auto flowState = getPageFlowState(g_externalAssets, -previousPageId - 1, WidgetCursor());
            if (flowState) {
                onEvent(flowState, FLOW_EVENT_CLOSE_PAGE, Value());
            }
        }
    }
    if (!activePageIsFromStack) {
        if (activePageId > 0 && activePageId < FIRST_INTERNAL_PAGE_ID) {
            auto flowState = getPageFlowState(g_mainAssets, activePageId - 1, WidgetCursor());
            if (flowState) {
                onEvent(flowState, FLOW_EVENT_OPEN_PAGE, Value());
            }
        } else if (activePageId < 0) {
            auto flowState = getPageFlowState(g_externalAssets, -activePageId - 1, WidgetCursor());
            if (flowState) {
                onEvent(flowState, FLOW_EVENT_OPEN_PAGE, Value());
            }
        }
    }
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_PAGE_CHANGED)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%d\t%d\n",
            MESSAGE_TO_DEBUGGER_PAGE_CHANGED,
            activePageId
        );
        writeDebuggerBufferHook(buffer, strlen(buffer));
    }
}
#else
void onPageChanged(int previousPageId, int activePageId, bool activePageIsFromStack, bool previousPageIsStillOnStack) {
    if (flow::isFlowStopped()) {
        return;
    }
    if (previousPageId == activePageId) {
        return;
    }
    if (!previousPageIsStillOnStack) {
        if (previousPageId > 0) {
            auto flowState = getPageFlowState(g_mainAssets, previousPageId - 1);
            if (flowState) {
                onEvent(flowState, FLOW_EVENT_CLOSE_PAGE, Value());
            }
        }
    }
    if (!activePageIsFromStack) {
        if (activePageId > 0) {
            auto flowState = getPageFlowState(g_mainAssets, activePageId - 1);
            if (flowState) {
                onEvent(flowState, FLOW_EVENT_OPEN_PAGE, Value());
            }
        }
    }
	if (isSubscribedTo(MESSAGE_TO_DEBUGGER_PAGE_CHANGED)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "%d\t%d\n",
            MESSAGE_TO_DEBUGGER_PAGE_CHANGED,
            activePageId
        );
        writeDebuggerBufferHook(buffer, strlen(buffer));
    }
}
#endif 
} 
} 
// -----------------------------------------------------------------------------
// flow/expression.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
#if EEZ_OPTION_GUI
using namespace eez::gui;
#endif
namespace eez {
namespace flow {
EvalStack g_stack;
static void evalExpression(FlowState *flowState, const uint8_t *instructions, int *numInstructionBytes, const char *errorMessage) {
	auto flowDefinition = flowState->flowDefinition;
	auto flow = flowState->flow;
	int i = 0;
	while (true) {
		uint16_t instruction = instructions[i] + (instructions[i + 1] << 8);
		auto instructionType = instruction & EXPR_EVAL_INSTRUCTION_TYPE_MASK;
		auto instructionArg = instruction & EXPR_EVAL_INSTRUCTION_PARAM_MASK;
		if (instructionType == EXPR_EVAL_INSTRUCTION_TYPE_PUSH_CONSTANT) {
			g_stack.push(*flowDefinition->constants[instructionArg]);
		} else if (instructionType == EXPR_EVAL_INSTRUCTION_TYPE_PUSH_INPUT) {
			g_stack.push(flowState->values[instructionArg]);
		} else if (instructionType == EXPR_EVAL_INSTRUCTION_TYPE_PUSH_LOCAL_VAR) {
			g_stack.push(&flowState->values[flow->componentInputs.count + instructionArg]);
		} else if (instructionType == EXPR_EVAL_INSTRUCTION_TYPE_PUSH_GLOBAL_VAR) {
			if ((uint32_t)instructionArg < flowDefinition->globalVariables.count) {
                if (g_globalVariables) {
				    g_stack.push(g_globalVariables->values + instructionArg);
                } else {
                    g_stack.push(flowDefinition->globalVariables[instructionArg]);
                }
			} else {
				g_stack.push(Value((int)(instructionArg - flowDefinition->globalVariables.count + 1), VALUE_TYPE_NATIVE_VARIABLE));
			}
		} else if (instructionType == EXPR_EVAL_INSTRUCTION_TYPE_PUSH_OUTPUT) {
			g_stack.push(Value((uint16_t)instructionArg, VALUE_TYPE_FLOW_OUTPUT));
		} else if (instructionType == EXPR_EVAL_INSTRUCTION_ARRAY_ELEMENT) {
			auto elementIndexValue = g_stack.pop().getValue();
			auto arrayValue = g_stack.pop().getValue();
            if (arrayValue.getType() == VALUE_TYPE_UNDEFINED || arrayValue.getType() == VALUE_TYPE_NULL) {
                g_stack.push(Value(0, VALUE_TYPE_UNDEFINED));
            } else {
                if (arrayValue.isArray()) {
                    auto array = arrayValue.getArray();
                    int err;
                    auto elementIndex = elementIndexValue.toInt32(&err);
                    if (!err) {
                        if (elementIndex >= 0 && elementIndex < (int)array->arraySize) {
                            g_stack.push(Value::makeArrayElementRef(arrayValue, elementIndex, 0x132e0e2f));
                        } else {
                            g_stack.push(Value::makeError());
                            g_stack.setErrorMessage("Array element index out of bounds\n");
                        }
                    } else {
                        g_stack.push(Value::makeError());
                        g_stack.setErrorMessage("Integer value expected for array element index\n");
                    }
                } else if (arrayValue.isBlob()) {
                    auto blobRef = arrayValue.getBlob();
                    int err;
                    auto elementIndex = elementIndexValue.toInt32(&err);
                    if (!err) {
                        if (elementIndex >= 0 && elementIndex < (int)blobRef->len) {
                            g_stack.push(Value::makeArrayElementRef(arrayValue, elementIndex, 0x132e0e2f));
                        } else {
                            g_stack.push(Value::makeError());
                            g_stack.setErrorMessage("Blob element index out of bounds\n");
                        }
                    } else {
                        g_stack.push(Value::makeError());
                        g_stack.setErrorMessage("Integer value expected for blob element index\n");
                    }
                } else {
                    g_stack.push(Value::makeError());
                    g_stack.setErrorMessage("Array value expected\n");
                }
            }
		} else if (instructionType == EXPR_EVAL_INSTRUCTION_TYPE_OPERATION) {
			g_evalOperations[instructionArg](g_stack);
		} else {
            if (instruction == EXPR_EVAL_INSTRUCTION_TYPE_END_WITH_DST_VALUE_TYPE) {
    			i += 2;
                if (g_stack.sp == 1) {
                    auto finalResult = g_stack.pop();
                    if (finalResult.getType() == VALUE_TYPE_VALUE_PTR) {
                        finalResult.dstValueType =
                            instructions[i] +
                            (instructions[i + 1] << 8) +
                            (instructions[i + 2] << 16) +
                            (instructions[i + 3] << 24);
                    }
                    g_stack.push(finalResult);
                }
                i += 4;
                break;
            } else {
			    i += 2;
			    break;
            }
		}
		i += 2;
	}
	if (numInstructionBytes) {
		*numInstructionBytes = i;
	}
}
#if EEZ_OPTION_GUI
bool evalExpression(FlowState *flowState, int componentIndex, const uint8_t *instructions, Value &result, const char *errorMessage, int *numInstructionBytes, const int32_t *iterators, DataOperationEnum operation) {
#else
bool evalExpression(FlowState *flowState, int componentIndex, const uint8_t *instructions, Value &result, const char *errorMessage, int *numInstructionBytes, const int32_t *iterators) {
#endif
	g_stack.sp = 0;
	g_stack.flowState = flowState;
	g_stack.componentIndex = componentIndex;
	g_stack.iterators = iterators;
    g_stack.errorMessage[0] = 0;
	evalExpression(flowState, instructions, numInstructionBytes, errorMessage);
    if (g_stack.sp == 1) {
#if EEZ_OPTION_GUI
        if (operation == DATA_OPERATION_GET_TEXT_REFRESH_RATE) {
            result = g_stack.pop();
            if (!result.isError()) {
                if (result.getType() == VALUE_TYPE_NATIVE_VARIABLE) {
                    auto nativeVariableId = result.getInt();
                    result = Value(getTextRefreshRate(g_widgetCursor, nativeVariableId), VALUE_TYPE_UINT32);
                } else {
                    result = 0;
                }
                return true;
            }
        } else if (operation == DATA_OPERATION_GET_TEXT_CURSOR_POSITION) {
            result = g_stack.pop();
            if (!result.isError()) {
                if (result.getType() == VALUE_TYPE_NATIVE_VARIABLE) {
                    auto nativeVariableId = result.getInt();
                    result = Value(getTextCursorPosition(g_widgetCursor, nativeVariableId), VALUE_TYPE_INT32);
                } else {
                    result = Value();
                }
                return true;
            }
        } else {
#endif
            result = g_stack.pop().getValue();
            if (!result.isError()) {
                return true;
            }
#if EEZ_OPTION_GUI
        }
#endif
    }
    throwError(flowState, componentIndex, errorMessage, *g_stack.errorMessage ? g_stack.errorMessage : nullptr);
	return false;
}
bool evalAssignableExpression(FlowState *flowState, int componentIndex, const uint8_t *instructions, Value &result, const char *errorMessage, int *numInstructionBytes, const int32_t *iterators) {
	g_stack.sp = 0;
	g_stack.flowState = flowState;
	g_stack.componentIndex = componentIndex;
	g_stack.iterators = iterators;
    g_stack.errorMessage[0] = 0;
	evalExpression(flowState, instructions, numInstructionBytes, errorMessage);
    if (g_stack.sp == 1) {
        auto finalResult = g_stack.pop();
        if (
            finalResult.getType() == VALUE_TYPE_VALUE_PTR ||
            finalResult.getType() == VALUE_TYPE_NATIVE_VARIABLE ||
            finalResult.getType() == VALUE_TYPE_FLOW_OUTPUT ||
            finalResult.getType() == VALUE_TYPE_ARRAY_ELEMENT_VALUE ||
            finalResult.getType() == VALUE_TYPE_JSON_MEMBER_VALUE
        ) {
            result = finalResult;
            return true;
        }
    }
    throwError(flowState, componentIndex, errorMessage, *g_stack.errorMessage ? g_stack.errorMessage : nullptr);
	return false;
}
#if EEZ_OPTION_GUI
bool evalProperty(FlowState *flowState, int componentIndex, int propertyIndex, Value &result, const char *errorMessage, int *numInstructionBytes, const int32_t *iterators, DataOperationEnum operation) {
#else
bool evalProperty(FlowState *flowState, int componentIndex, int propertyIndex, Value &result, const char *errorMessage, int *numInstructionBytes, const int32_t *iterators) {
#endif
    if (componentIndex < 0 || componentIndex >= (int)flowState->flow->components.count) {
        char message[256];
        snprintf(message, sizeof(message), "invalid component index %d in flow at index %d", componentIndex, flowState->flowIndex);
        throwError(flowState, componentIndex, errorMessage, message);
        return false;
    }
    auto component = flowState->flow->components[componentIndex];
    if (propertyIndex < 0 || propertyIndex >= (int)component->properties.count) {
        char message[256];
        snprintf(message, sizeof(message), "invalid property index %d at component index %d in flow at index %d", propertyIndex, componentIndex, flowState->flowIndex);
        throwError(flowState, componentIndex, errorMessage, message);
        return false;
    }
#if EEZ_OPTION_GUI
    return evalExpression(flowState, componentIndex, component->properties[propertyIndex]->evalInstructions, result, errorMessage, numInstructionBytes, iterators, operation);
#else
    return evalExpression(flowState, componentIndex, component->properties[propertyIndex]->evalInstructions, result, errorMessage, numInstructionBytes, iterators);
#endif
}
bool evalAssignableProperty(FlowState *flowState, int componentIndex, int propertyIndex, Value &result, const char *errorMessage, int *numInstructionBytes, const int32_t *iterators) {
    if (componentIndex < 0 || componentIndex >= (int)flowState->flow->components.count) {
        char message[256];
        snprintf(message, sizeof(message), "invalid component index %d in flow at index %d", componentIndex, flowState->flowIndex);
        throwError(flowState, componentIndex, errorMessage, message);
        return false;
    }
    auto component = flowState->flow->components[componentIndex];
    if (propertyIndex < 0 || propertyIndex >= (int)component->properties.count) {
        char message[256];
        snprintf(message, sizeof(message), "invalid property index %d (max: %d) in component at index %d in flow at index %d", propertyIndex, (int)component->properties.count, componentIndex, flowState->flowIndex);
        throwError(flowState, componentIndex, errorMessage, message);
        return false;
    }
    return evalAssignableExpression(flowState, componentIndex, component->properties[propertyIndex]->evalInstructions, result, errorMessage, numInstructionBytes, iterators);
}
#if EEZ_OPTION_GUI
int16_t getNativeVariableId(const WidgetCursor &widgetCursor) {
	if (widgetCursor.flowState) {
		FlowState *flowState = widgetCursor.flowState;
		auto flow = flowState->flow;
		WidgetDataItem *widgetDataItem = flow->widgetDataItems[-(widgetCursor.widget->data + 1)];
		if (widgetDataItem && widgetDataItem->componentIndex != -1 && widgetDataItem->propertyValueIndex != -1) {
			auto component = flow->components[widgetDataItem->componentIndex];
			auto property = component->properties[widgetDataItem->propertyValueIndex];
			g_stack.sp = 0;
			g_stack.flowState = flowState;
			g_stack.componentIndex = widgetDataItem->componentIndex;
			g_stack.iterators = widgetCursor.iterators;
            g_stack.errorMessage[0] = 0;
			evalExpression(flowState, property->evalInstructions, nullptr, nullptr);
            if (g_stack.sp == 1) {
                auto finalResult = g_stack.pop();
                if (finalResult.getType() == VALUE_TYPE_NATIVE_VARIABLE) {
                    return finalResult.getInt();
                }
            }
		}
	}
	return DATA_ID_NONE;
}
#endif
} 
} 
// -----------------------------------------------------------------------------
// flow/flow.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
#if EEZ_OPTION_GUI
using namespace eez::gui;
#endif
#if defined(EEZ_DASHBOARD_API)
#endif
namespace eez {
namespace flow {
#if defined(__EMSCRIPTEN__)
uint32_t g_wasmModuleId = 0;
#endif
static const uint32_t FLOW_TICK_MAX_DURATION_MS = 5;
int g_selectedLanguage = 0;
FlowState *g_firstFlowState;
FlowState *g_lastFlowState;
static bool g_isStopping = false;
static bool g_isStopped = true;
static void doStop();
unsigned start(Assets *assets) {
	auto flowDefinition = static_cast<FlowDefinition *>(assets->flowDefinition);
	if (flowDefinition->flows.count == 0) {
		return 0;
	}
    g_isStopped = false;
    g_isStopping = false;
    initGlobalVariables(assets);
	queueReset();
    watchListReset();
	scpiComponentInitHook();
	onStarted(assets);
	return 1;
}
void tick() {
	if (isFlowStopped()) {
		return;
	}
    if (g_isStopping) {
        doStop();
        return;
    }
	uint32_t startTickCount = millis();
    auto n = getQueueSize();
    for (size_t i = 0; i < n || g_numContinuousTaskInQueue > 0; i++) {
		FlowState *flowState;
		unsigned componentIndex;
        bool continuousTask;
		if (!peekNextTaskFromQueue(flowState, componentIndex, continuousTask)) {
			break;
		}
		if (!continuousTask && !canExecuteStep(flowState, componentIndex)) {
			break;
		}
		removeNextTaskFromQueue();
        flowState->executingComponentIndex = componentIndex;
        if (flowState->error) {
            deallocateComponentExecutionState(flowState, componentIndex);
        } else {
            if (continuousTask) {
                auto componentExecutionState = (ComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
                if (!componentExecutionState) {
                    executeComponent(flowState, componentIndex);
                } else if (componentExecutionState->lastExecutedTime + FLOW_TICK_MAX_DURATION_MS <= startTickCount) {
                    componentExecutionState->lastExecutedTime = startTickCount;
                    executeComponent(flowState, componentIndex);
                } else {
                    addToQueue(flowState, componentIndex, -1, -1, -1, true);
                }
            } else {
                executeComponent(flowState, componentIndex);
            }
        }
        if (isFlowStopped() || g_isStopping) {
            break;
        }
        resetSequenceInputs(flowState);
        if (canFreeFlowState(flowState)) {
            freeFlowState(flowState);
        }
        if ((i + 1) % 5 == 0) {
            if (millis() - startTickCount >= FLOW_TICK_MAX_DURATION_MS) {
                break;
            }
        }
	}
    visitWatchList();
	finishToDebuggerMessageHook();
}
void stop() {
    g_isStopping = true;
}
void doStop() {
    onStopped();
    finishToDebuggerMessageHook();
    g_debuggerIsConnected = false;
    freeAllChildrenFlowStates(g_firstFlowState);
    g_firstFlowState = nullptr;
    g_lastFlowState = nullptr;
    g_isStopped = true;
	queueReset();
    watchListReset();
}
bool isFlowStopped() {
    return g_isStopped;
}
#if EEZ_OPTION_GUI
FlowState *getPageFlowState(Assets *assets, int16_t pageIndex, const WidgetCursor &widgetCursor) {
	if (!assets->flowDefinition) {
		return nullptr;
	}
	if (isFlowStopped()) {
		return nullptr;
	}
	if (widgetCursor.widget && widgetCursor.widget->type == WIDGET_TYPE_USER_WIDGET) {
		if (widgetCursor.flowState) {
			auto userWidgetWidget = (UserWidgetWidget *)widgetCursor.widget;
			auto flowState = widgetCursor.flowState;
			auto userWidgetWidgetComponentIndex = userWidgetWidget->componentIndex;
			return getUserWidgetFlowState(flowState, userWidgetWidgetComponentIndex, pageIndex);
		}
	} else {
		auto page = assets->pages[pageIndex];
		if (!(page->flags & PAGE_IS_USED_AS_USER_WIDGET)) {
            FlowState *flowState;
            for (flowState = g_firstFlowState; flowState; flowState = flowState->nextSibling) {
                if (flowState->flowIndex == pageIndex) {
                    break;
                }
            }
            if (!flowState) {
				flowState = initPageFlowState(assets, pageIndex, nullptr, 0);
			}
			return flowState;
		}
	}
	return nullptr;
}
#else
FlowState *getPageFlowState(Assets *assets, int16_t pageIndex) {
	if (!assets->flowDefinition) {
		return nullptr;
	}
	if (isFlowStopped()) {
		return nullptr;
	}
    FlowState *flowState;
    for (flowState = g_firstFlowState; flowState; flowState = flowState->nextSibling) {
        if (flowState->flowIndex == pageIndex) {
            break;
        }
    }
    if (!flowState) {
        flowState = initPageFlowState(assets, pageIndex, nullptr, 0);
    }
    return flowState;
}
#endif 
int getPageIndex(FlowState *flowState) {
	return flowState->flowIndex;
}
Value getGlobalVariable(uint32_t globalVariableIndex) {
    return getGlobalVariable(g_mainAssets, globalVariableIndex);
}
Value getGlobalVariable(Assets *assets, uint32_t globalVariableIndex) {
    if (globalVariableIndex >= 0 && globalVariableIndex < assets->flowDefinition->globalVariables.count) {
        return g_globalVariables ? g_globalVariables->values[globalVariableIndex] : *assets->flowDefinition->globalVariables[globalVariableIndex];
    }
    return Value();
}
void setGlobalVariable(uint32_t globalVariableIndex, const Value &value) {
    setGlobalVariable(g_mainAssets, globalVariableIndex, value);
}
void setGlobalVariable(Assets *assets, uint32_t globalVariableIndex, const Value &value) {
    if (globalVariableIndex >= 0 && globalVariableIndex < assets->flowDefinition->globalVariables.count) {
        if (g_globalVariables) {
            g_globalVariables->values[globalVariableIndex] = value;
        } else {
            *assets->flowDefinition->globalVariables[globalVariableIndex] = value;
        }
    }
}
#if EEZ_OPTION_GUI
void executeFlowAction(const WidgetCursor &widgetCursor, int16_t actionId, void *param) {
	if (isFlowStopped()) {
		return;
	}
	auto flowState = widgetCursor.flowState;
	actionId = -actionId - 1;
	auto flow = flowState->flow;
	if (actionId >= 0 && actionId < (int16_t)flow->widgetActions.count) {
		auto componentOutput = flow->widgetActions[actionId];
		if (componentOutput->componentIndex != -1 && componentOutput->componentOutputIndex != -1) {
            if (widgetCursor.widget->type == WIDGET_TYPE_DROP_DOWN_LIST) {
                auto params = Value::makeArrayRef(defs_v3::SYSTEM_STRUCTURE_DROP_DOWN_LIST_CHANGE_EVENT_NUM_FIELDS, defs_v3::SYSTEM_STRUCTURE_DROP_DOWN_LIST_CHANGE_EVENT, 0x53e3b30b);
                ((ArrayValueRef *)params.refValue)->arrayValue.values[defs_v3::SYSTEM_STRUCTURE_DROP_DOWN_LIST_CHANGE_EVENT_FIELD_INDEX] = widgetCursor.iterators[0];
                auto indexes = Value::makeArrayRef(MAX_ITERATORS, defs_v3::ARRAY_TYPE_INTEGER, 0xb1f68ef8);
                for (size_t i = 0; i < MAX_ITERATORS; i++) {
                    ((ArrayValueRef *)indexes.refValue)->arrayValue.values[i] = (int)widgetCursor.iterators[i];
                }
                ((ArrayValueRef *)params.refValue)->arrayValue.values[defs_v3::SYSTEM_STRUCTURE_DROP_DOWN_LIST_CHANGE_EVENT_FIELD_INDEXES] = indexes;
                ((ArrayValueRef *)params.refValue)->arrayValue.values[defs_v3::SYSTEM_STRUCTURE_DROP_DOWN_LIST_CHANGE_EVENT_FIELD_SELECTED_INDEX] = *((int *)param);
                propagateValue(flowState, componentOutput->componentIndex, componentOutput->componentOutputIndex, params);
            } else {
                auto params = Value::makeArrayRef(defs_v3::SYSTEM_STRUCTURE_CLICK_EVENT_NUM_FIELDS, defs_v3::SYSTEM_STRUCTURE_CLICK_EVENT, 0x285940bb);
                ((ArrayValueRef *)params.refValue)->arrayValue.values[defs_v3::SYSTEM_STRUCTURE_CLICK_EVENT_FIELD_INDEX] = widgetCursor.iterators[0];
                auto indexes = Value::makeArrayRef(MAX_ITERATORS, defs_v3::ARRAY_TYPE_INTEGER, 0xb1f68ef8);
                for (size_t i = 0; i < MAX_ITERATORS; i++) {
                    ((ArrayValueRef *)indexes.refValue)->arrayValue.values[i] = (int)widgetCursor.iterators[i];
                }
                ((ArrayValueRef *)params.refValue)->arrayValue.values[defs_v3::SYSTEM_STRUCTURE_CLICK_EVENT_FIELD_INDEXES] = indexes;
                propagateValue(flowState, componentOutput->componentIndex, componentOutput->componentOutputIndex, params);
            }
		} else if (componentOutput->componentOutputIndex != -1) {
            propagateValue(flowState, componentOutput->componentIndex, componentOutput->componentOutputIndex);
        }
	}
	for (int i = 0; i < 3; i++) {
		tick();
	}
}
void dataOperation(int16_t dataId, DataOperationEnum operation, const WidgetCursor &widgetCursor, Value &value) {
	if (isFlowStopped()) {
		return;
	}
	auto flowState = widgetCursor.flowState;
	auto flowDataId = -dataId - 1;
	auto flow = flowState->flow;
	if (flowDataId >= 0 && flowDataId < (int16_t)flow->widgetDataItems.count) {
		WidgetDataItem *widgetDataItem = flow->widgetDataItems[flowDataId];
		auto component = flow->components[widgetDataItem->componentIndex];
		if (operation == DATA_OPERATION_GET) {
			getValue(flowDataId, operation, widgetCursor, value);
			if (component->type == WIDGET_TYPE_INPUT && dataId == widgetCursor.widget->data) {
				value = getInputWidgetData(widgetCursor, value);
			}
		} else if (operation == DATA_OPERATION_COUNT) {
			Value arrayValue;
			getValue(flowDataId, operation, widgetCursor, arrayValue);
			if (arrayValue.isArray()) {
                auto array = arrayValue.getArray();
                if (array->arrayType == defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE) {
                    value = array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_NUM_ITEMS];
                } else {
				    value = array->arraySize;
                }
			} else {
                value = arrayValue;
            }
		}
		else if (operation == DATA_OPERATION_GET_MIN) {
			if (component->type == WIDGET_TYPE_INPUT) {
				value = getInputWidgetMin(widgetCursor);
			}
		} else if (operation == DATA_OPERATION_GET_MAX) {
			if (component->type == WIDGET_TYPE_INPUT) {
				value = getInputWidgetMax(widgetCursor);
			}
		} else if (operation == DATA_OPERATION_GET_PRECISION) {
			if (component->type == WIDGET_TYPE_INPUT) {
				value = getInputWidgetPrecision(widgetCursor);
			}
		} else if (operation == DATA_OPERATION_GET_UNIT) {
			if (component->type == WIDGET_TYPE_INPUT) {
				value = getBaseUnit(getInputWidgetUnit(widgetCursor));
			}
		} else if (operation == DATA_OPERATION_SET) {
			if (component->type == WIDGET_TYPE_INPUT) {
				auto inputWidget = (InputWidget *)widgetCursor.widget;
				if (inputWidget->flags & INPUT_WIDGET_TYPE_NUMBER) {
					if (value.isInt32()) {
						setValue(flowDataId, widgetCursor, value);
					} else {
						Value precisionValue = getInputWidgetPrecision(widgetCursor);
						float precision = precisionValue.toFloat();
						float valueFloat = value.toFloat();
						Unit unit = getInputWidgetUnit(widgetCursor);
						setValue(flowDataId, widgetCursor, Value(roundPrec(valueFloat, precision) / getUnitFactor(unit), VALUE_TYPE_FLOAT));
					}
				} else {
					setValue(flowDataId, widgetCursor, value);
				}
				executeFlowAction(widgetCursor, inputWidget->action, nullptr);
			} else {
				setValue(flowDataId, widgetCursor, value);
			}
		} else if (operation == DATA_OPERATION_YT_DATA_GET_SIZE) {
            Value arrayValue;
            getValue(flowDataId, operation, widgetCursor, arrayValue);
            if (arrayValue.isArray()) {
                auto array = arrayValue.getArray();
                if (array->arrayType == defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE) {
                    value = array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_NUM_ITEMS].toInt32();
                } else {
                    value = 0;
                }
            } else {
                value = 0;
            }
        } else if (operation == DATA_OPERATION_YT_DATA_GET_PAGE_SIZE) {
            Value arrayValue;
            getValue(flowDataId, operation, widgetCursor, arrayValue);
            if (arrayValue.isArray()) {
                auto array = arrayValue.getArray();
                if (array->arrayType == defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE) {
                    value = array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_ITEMS_PER_PAGE].toInt32();
                } else {
                    value = 0;
                }
            } else {
                value = 0;
            }
        } else if (operation == DATA_OPERATION_YT_DATA_GET_POSITION_INCREMENT) {
            Value arrayValue;
            getValue(flowDataId, operation, widgetCursor, arrayValue);
            if (arrayValue.isArray()) {
                auto array = arrayValue.getArray();
                if (array->arrayType == defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE) {
                    value = array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_POSITION_INCREMENT].toInt32();
                } else {
                    value = 0;
                }
            } else {
                value = 0;
            }
        } else if (operation == DATA_OPERATION_YT_DATA_GET_POSITION) {
            Value arrayValue;
            getValue(flowDataId, operation, widgetCursor, arrayValue);
            if (arrayValue.isArray()) {
                auto array = arrayValue.getArray();
                if (array->arrayType == defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE) {
                    value = array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_POSITION].toInt32();
                } else {
                    value = 0;
                }
            } else {
                value = 0;
            }
        } else if (operation == DATA_OPERATION_YT_DATA_SET_POSITION) {
            Value arrayValue;
            getValue(flowDataId, operation, widgetCursor, arrayValue);
            if (arrayValue.isArray()) {
                auto array = arrayValue.getArray();
                if (array->arrayType == defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE) {
                    auto newPosition = value.getInt();
                    auto numItems = array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_NUM_ITEMS].getInt();
                    auto itemsPerPage = array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_ITEMS_PER_PAGE].getInt();
                    if (newPosition < 0) {
                        newPosition = 0;
                    } else if (newPosition > numItems - itemsPerPage) {
                        newPosition = numItems - itemsPerPage;
                    }
                    array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_POSITION] = newPosition;
                    onValueChanged(&array->values[defs_v3::SYSTEM_STRUCTURE_SCROLLBAR_STATE_FIELD_POSITION]);
                } else {
                    value = 0;
                }
            } else {
                value = 0;
            }
        } else if (operation == DATA_OPERATION_GET_TEXT_REFRESH_RATE) {
            getValue(flowDataId, operation, widgetCursor, value);
        } else if (operation == DATA_OPERATION_GET_BITMAP_IMAGE) {
            getValue(flowDataId, operation, widgetCursor, value);
        }
#if OPTION_KEYPAD
		else if (operation == DATA_OPERATION_GET_TEXT_CURSOR_POSITION) {
            getValue(flowDataId, operation, widgetCursor, value);
		}
#endif
	} else {
		value = Value();
	}
}
#endif 
void onArrayValueFree(ArrayValue *arrayValue) {
#if defined(EEZ_DASHBOARD_API)
    if (g_dashboardValueFree) {
        return;
    }
#endif
    if (arrayValue->arrayType == defs_v3::OBJECT_TYPE_MQTT_CONNECTION) {
        onFreeMQTTConnection(arrayValue);
    }
    const uint32_t CATEGORY_SHIFT = 13;
    const uint32_t CATEGORY_MASK = 0x7;
    const uint32_t CATEGORY_OBJECT = 5;
#if defined(EEZ_DASHBOARD_API)
    if (((arrayValue->arrayType >> CATEGORY_SHIFT) & CATEGORY_MASK) == CATEGORY_OBJECT) {
        eez::flow::onObjectArrayValueFree(arrayValue);
    }
#endif
}
} 
} 
// -----------------------------------------------------------------------------
// flow/hooks.cpp
// -----------------------------------------------------------------------------
#include <assert.h>
#include <math.h>
#if EEZ_OPTION_GUI
#endif
#include <chrono>
namespace eez {
namespace flow {
static void replacePage(int16_t pageId, uint32_t animType, uint32_t speed, uint32_t delay) {
#if EEZ_OPTION_GUI
	eez::gui::getAppContextFromId(APP_CONTEXT_ID_DEVICE)->replacePage(pageId);
#endif
}
static void showKeyboard(Value label, Value initialText, Value minChars, Value maxChars, bool isPassword, void(*onOk)(char *), void(*onCancel)()) {
}
static void showKeypad(Value label, Value initialValue, Value min, Value max, Unit unit, void(*onOk)(float), void(*onCancel)()) {
}
static void stopScript() {
	assert(false);
}
static void scpiComponentInit() {
}
static void startToDebuggerMessage() {
}
static void writeDebuggerBuffer(const char *buffer, uint32_t length) {
}
static void finishToDebuggerMessage() {
}
static void onDebuggerInputAvailable() {
}
void (*replacePageHook)(int16_t pageId, uint32_t animType, uint32_t speed, uint32_t delay) = replacePage;
void (*showKeyboardHook)(Value label, Value initialText, Value minChars, Value maxChars, bool isPassword, void(*onOk)(char *), void(*onCancel)()) = showKeyboard;
void (*showKeypadHook)(Value label, Value initialValue, Value min, Value max, Unit unit, void(*onOk)(float), void(*onCancel)()) = showKeypad;
void (*stopScriptHook)() = stopScript;
void (*scpiComponentInitHook)() = scpiComponentInit;
void (*startToDebuggerMessageHook)() = startToDebuggerMessage;
void (*writeDebuggerBufferHook)(const char *buffer, uint32_t length) = writeDebuggerBuffer;
void (*finishToDebuggerMessageHook)() = finishToDebuggerMessage;
void (*onDebuggerInputAvailableHook)() = onDebuggerInputAvailable;
#if defined(EEZ_FOR_LVGL)
static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    return 0;
}
static const void *getLvglImageByName(const char *name) {
    return 0;
}
static void executeLvglAction(int actionIndex) {
}
lv_obj_t *(*getLvglObjectFromIndexHook)(int32_t index) = getLvglObjectFromIndex;
const void *(*getLvglImageByNameHook)(const char *name) = getLvglImageByName;
void (*executeLvglActionHook)(int actionIndex) = executeLvglAction;
#endif
double getDateNowDefaultImplementation() {
    using namespace std::chrono;
    milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
    return (double)ms.count();
}
double (*getDateNowHook)() = getDateNowDefaultImplementation;
void (*onFlowErrorHook)(FlowState *flowState, int componentIndex, const char *errorMessage) = nullptr;
} 
} 
// -----------------------------------------------------------------------------
// flow/lvgl_api.cpp
// -----------------------------------------------------------------------------
#if defined(EEZ_FOR_LVGL)
#include <stdio.h>
#if defined(EEZ_FOR_LVGL)
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#endif
static void replacePageHook(int16_t pageId, uint32_t animType, uint32_t speed, uint32_t delay);
extern "C" void create_screens();
extern "C" void tick_screen(int screen_index);
static lv_obj_t **g_objects;
static size_t g_numObjects;
static const ext_img_desc_t *g_images;
static size_t g_numImages;
static ActionExecFunc *g_actions;
int16_t g_currentScreen = -1;
static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return g_objects[index];
}
static const void *getLvglImageByName(const char *name) {
    for (size_t imageIndex = 0; imageIndex < g_numImages; imageIndex++) {
        if (strcmp(g_images[imageIndex].name, name) == 0) {
            return g_images[imageIndex].img_dsc;
        }
    }
    return 0;
}
static void executeLvglAction(int actionIndex) {
    g_actions[actionIndex](0);
}
#if !defined(EEZ_LVGL_SCREEN_STACK_SIZE)
#define EEZ_LVGL_SCREEN_STACK_SIZE 10
#endif
int16_t g_screenStack[EEZ_LVGL_SCREEN_STACK_SIZE];
unsigned g_screenStackPosition = 0;
extern "C" int16_t eez_flow_get_current_screen() {
    return g_currentScreen + 1;
}
extern "C" void eez_flow_set_screen(int16_t screenId, lv_scr_load_anim_t animType, uint32_t speed, uint32_t delay) {
    g_screenStackPosition = 0;
    eez::flow::replacePageHook(screenId, animType, speed, delay);
}
extern "C" void eez_flow_push_screen(int16_t screenId, lv_scr_load_anim_t animType, uint32_t speed, uint32_t delay) {
    if (g_screenStackPosition == EEZ_LVGL_SCREEN_STACK_SIZE) {
        for (unsigned i = 1; i < EEZ_LVGL_SCREEN_STACK_SIZE; i++) {
            g_screenStack[i - 1] = g_screenStack[i];
        }
        g_screenStackPosition--;
    }
    g_screenStack[g_screenStackPosition++] = g_currentScreen + 1;
    eez::flow::replacePageHook(screenId, animType, speed, delay);
}
extern "C" void eez_flow_pop_screen(lv_scr_load_anim_t animType, uint32_t speed, uint32_t delay) {
    if (g_screenStackPosition > 0) {
        g_screenStackPosition--;
        eez::flow::replacePageHook(g_screenStack[g_screenStackPosition], animType, speed, delay);
    }
}
extern "C" void eez_flow_init(const uint8_t *assets, uint32_t assetsSize, lv_obj_t **objects, size_t numObjects, const ext_img_desc_t *images, size_t numImages, ActionExecFunc *actions) {
    g_objects = objects;
    g_numObjects = numObjects;
    g_images = images;
    g_numImages = numImages;
    g_actions = actions;
    eez::initAssetsMemory();
    eez::loadMainAssets(assets, assetsSize);
    eez::initOtherMemory();
    eez::initAllocHeap(eez::ALLOC_BUFFER, eez::ALLOC_BUFFER_SIZE);
    eez::flow::replacePageHook = replacePageHook;
    eez::flow::getLvglObjectFromIndexHook = getLvglObjectFromIndex;
    eez::flow::getLvglImageByNameHook = getLvglImageByName;
    eez::flow::executeLvglActionHook = executeLvglAction;
    eez::flow::start(eez::g_mainAssets);
    create_screens();
    replacePageHook(1, 0, 0, 0);
}
extern "C" void eez_flow_tick() {
    eez::flow::tick();
}
extern "C" bool eez_flow_is_stopped() {
    return eez::flow::isFlowStopped();
}
namespace eez {
ActionExecFunc g_actionExecFunctions[] = { 0 };
}
void replacePageHook(int16_t pageId, uint32_t animType, uint32_t speed, uint32_t delay) {
    eez::flow::onPageChanged(g_currentScreen + 1, pageId);
    g_currentScreen = pageId - 1;
    lv_scr_load_anim(getLvglObjectFromIndex(g_currentScreen), (lv_scr_load_anim_t)animType, speed, delay, false);
}
extern "C" void flowOnPageLoaded(unsigned pageIndex) {
    eez::flow::getPageFlowState(eez::g_mainAssets, pageIndex);
}
extern "C" void flowPropagateValue(void *flowState, unsigned componentIndex, unsigned outputIndex) {
    eez::flow::propagateValue((eez::flow::FlowState *)flowState, componentIndex, outputIndex);
}
#ifndef EEZ_LVGL_TEMP_STRING_BUFFER_SIZE
#define EEZ_LVGL_TEMP_STRING_BUFFER_SIZE 1024
#endif
static char textValue[EEZ_LVGL_TEMP_STRING_BUFFER_SIZE];
extern "C" const char *evalTextProperty(void *flowState, unsigned componentIndex, unsigned propertyIndex, const char *errorMessage) {
    eez::Value value;
    if (!eez::flow::evalProperty((eez::flow::FlowState *)flowState, componentIndex, propertyIndex, value, errorMessage)) {
        return "";
    }
    value.toText(textValue, sizeof(textValue));
    return textValue;
}
extern "C" int32_t evalIntegerProperty(void *flowState, unsigned componentIndex, unsigned propertyIndex, const char *errorMessage) {
    eez::Value value;
    if (!eez::flow::evalProperty((eez::flow::FlowState *)flowState, componentIndex, propertyIndex, value, errorMessage)) {
        return 0;
    }
    int err;
    int32_t intValue = value.toInt32(&err);
    if (err) {
        eez::flow::throwError((eez::flow::FlowState *)flowState, componentIndex, errorMessage);
        return 0;
    }
    return intValue;
}
extern "C" bool evalBooleanProperty(void *flowState, unsigned componentIndex, unsigned propertyIndex, const char *errorMessage) {
    eez::Value value;
    if (!eez::flow::evalProperty((eez::flow::FlowState *)flowState, componentIndex, propertyIndex, value, errorMessage)) {
        return 0;
    }
    int err;
    bool booleanValue = value.toBool(&err);
    if (err) {
        eez::flow::throwError((eez::flow::FlowState *)flowState, componentIndex, errorMessage);
        return 0;
    }
    return booleanValue;
}
const char *evalStringArrayPropertyAndJoin(void *flowState, unsigned componentIndex, unsigned propertyIndex, const char *errorMessage, const char *separator) {
    eez::Value value;
    if (!eez::flow::evalProperty((eez::flow::FlowState *)flowState, componentIndex, propertyIndex, value, errorMessage)) {
        return "";
    }
    if (value.isArray()) {
        auto array = value.getArray();
        textValue[0] = 0;
        size_t textPosition = 0;
        size_t separatorLength = strlen(separator);
        for (uint32_t elementIndex = 0; elementIndex < array->arraySize; elementIndex++) {
            if (elementIndex > 0) {
                eez::stringAppendString(textValue + textPosition, sizeof(textValue) - textPosition, separator);
                textPosition += separatorLength;
            }
            array->values[elementIndex].toText(textValue + textPosition, sizeof(textValue) - textPosition);
            textPosition = strlen(textValue);
        }
        return textValue;
    }
    return "";
}
extern "C" void assignStringProperty(void *flowState, unsigned componentIndex, unsigned propertyIndex, const char *value, const char *errorMessage) {
    auto component = ((eez::flow::FlowState *)flowState)->flow->components[componentIndex];
    eez::Value dstValue;
    if (!eez::flow::evalAssignableExpression((eez::flow::FlowState *)flowState, componentIndex, component->properties[propertyIndex]->evalInstructions, dstValue, errorMessage)) {
        return;
    }
    eez::Value srcValue = eez::Value::makeStringRef(value, -1, 0x3eefcf0d);
    eez::flow::assignValue((eez::flow::FlowState *)flowState, componentIndex, dstValue, srcValue);
}
extern "C" void assignIntegerProperty(void *flowState, unsigned componentIndex, unsigned propertyIndex, int32_t value, const char *errorMessage) {
    auto component = ((eez::flow::FlowState *)flowState)->flow->components[componentIndex];
    eez::Value dstValue;
    if (!eez::flow::evalAssignableExpression((eez::flow::FlowState *)flowState, componentIndex, component->properties[propertyIndex]->evalInstructions, dstValue, errorMessage)) {
        return;
    }
    eez::Value srcValue((int)value, eez::VALUE_TYPE_INT32);
    eez::flow::assignValue((eez::flow::FlowState *)flowState, componentIndex, dstValue, srcValue);
}
extern "C" void assignBooleanProperty(void *flowState, unsigned componentIndex, unsigned propertyIndex, bool value, const char *errorMessage) {
    auto component = ((eez::flow::FlowState *)flowState)->flow->components[componentIndex];
    eez::Value dstValue;
    if (!eez::flow::evalAssignableExpression((eez::flow::FlowState *)flowState, componentIndex, component->properties[propertyIndex]->evalInstructions, dstValue, errorMessage)) {
        return;
    }
    eez::Value srcValue(value, eez::VALUE_TYPE_BOOLEAN);
    eez::flow::assignValue((eez::flow::FlowState *)flowState, componentIndex, dstValue, srcValue);
}
extern "C" float getTimelinePosition(void *flowState) {
    return ((eez::flow::FlowState *)flowState)->timelinePosition;
}
void *getFlowState(void *flowState, unsigned userWidgetComponentIndexOrPageIndex) {
    if (!flowState) {
        return eez::flow::getPageFlowState(eez::g_mainAssets, userWidgetComponentIndexOrPageIndex);
    }
    auto executionState = (eez::flow::LVGLUserWidgetExecutionState *)((eez::flow::FlowState *)flowState)->componenentExecutionStates[userWidgetComponentIndexOrPageIndex];
    if (!executionState) {
        executionState = eez::flow::createUserWidgetFlowState((eez::flow::FlowState *)flowState, userWidgetComponentIndexOrPageIndex);
    }
    return executionState->flowState;
}
bool compareRollerOptions(lv_roller_t *roller, const char *new_val, const char *cur_val, lv_roller_mode_t mode) {
    if (mode == LV_ROLLER_MODE_NORMAL) {
        return strcmp(new_val, cur_val) != 0;
    }
    auto n = strlen(new_val);
#if LVGL_VERSION_MAJOR >= 9
    int numPages = roller->inf_page_cnt;
#else
    int numPages = LV_ROLLER_INF_PAGES;
#endif
    for (int i = 0; i < numPages * (n + 1); i += n + 1) {
        if (strncmp(new_val, cur_val + i, n) != 0) {
            return true;
        }
    }
    return false;
}
#endif 
// -----------------------------------------------------------------------------
// flow/operations.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include <string>
#if defined(EEZ_DASHBOARD_API)
#endif
#if defined(EEZ_FOR_LVGL)
#endif
#if EEZ_FOR_LVGL_SHA256_OPTION
extern "C" {
}
#endif
#if EEZ_OPTION_GUI
using namespace eez::gui;
#endif
int g_eezFlowLvlgMeterTickIndex = 0;
namespace eez {
namespace flow {
Value op_add(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (a.isBlob() || b.isBlob()) {
        if (a.isBlob()) {
            if (b.isUndefinedOrNull()) {
                return a;
            }
            if (!b.isBlob()) {
                return Value::makeError();
            }
        } else {
            if (a.isUndefinedOrNull()) {
                return b;
            }
            return Value::makeError();
        }
        auto aBlob = a.getBlob();
        auto bBlob = b.getBlob();
        return Value::makeBlobRef(aBlob->blob, aBlob->len, bBlob->blob, bBlob->len, 0xc622dd24);
    }
    auto a_valid = a.isString() || a.isDouble() || a.isFloat() || a.isInt64() || a.isInt32OrLess();
    auto b_valid = b.isString() || b.isDouble() || b.isFloat() || b.isInt64() || b.isInt32OrLess();
    if (!a_valid && !b_valid) {
        return Value::makeError();
    }
    if (a.isString() || b.isString()) {
        Value value1 = a.toString(0x84eafaa8);
        Value value2 = b.toString(0xd273cab6);
        auto res = Value::concatenateString(value1, value2);
        char str1[128];
        res.toText(str1, sizeof(str1));
        return res;
    }
    if (a.isDouble() || b.isDouble()) {
        return Value(a.toDouble() + b.toDouble(), VALUE_TYPE_DOUBLE);
    }
    if (a.isFloat() || b.isFloat()) {
        return Value(a.toFloat() + b.toFloat(), VALUE_TYPE_FLOAT);
    }
    if (a.isInt64() || b.isInt64()) {
        return Value(a.toInt64() + b.toInt64(), VALUE_TYPE_INT64);
    }
    return Value((int)(a.int32Value + b.int32Value), VALUE_TYPE_INT32);
}
Value op_sub(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isDouble() || a.isFloat() || a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isDouble() || b.isFloat() || b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isDouble() || b.isDouble()) {
        return Value(a.toDouble() - b.toDouble(), VALUE_TYPE_DOUBLE);
    }
    if (a.isFloat() || b.isFloat()) {
        return Value(a.toFloat() - b.toFloat(), VALUE_TYPE_FLOAT);
    }
    if (a.isInt64() || b.isInt64()) {
        return Value(a.toInt64() - b.toInt64(), VALUE_TYPE_INT64);
    }
    return Value((int)(a.int32Value - b.int32Value), VALUE_TYPE_INT32);
}
Value op_mul(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isDouble() || a.isFloat() || a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isDouble() || b.isFloat() || b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isDouble() || b.isDouble()) {
        return Value(a.toDouble() * b.toDouble(), VALUE_TYPE_DOUBLE);
    }
    if (a.isFloat() || b.isFloat()) {
        return Value(a.toFloat() * b.toFloat(), VALUE_TYPE_FLOAT);
    }
    if (a.isInt64() || b.isInt64()) {
        return Value(a.toInt64() * b.toInt64(), VALUE_TYPE_INT64);
    }
    return Value((int)(a.int32Value * b.int32Value), VALUE_TYPE_INT32);
}
Value op_div(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isDouble() || a.isFloat() || a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isDouble() || b.isFloat() || b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isDouble() || b.isDouble()) {
        return Value(a.toDouble() / b.toDouble(), VALUE_TYPE_DOUBLE);
    }
    if (a.isFloat() || b.isFloat()) {
        return Value(a.toFloat() / b.toFloat(), VALUE_TYPE_FLOAT);
    }
    if (a.isInt64() || b.isInt64()) {
        auto d = b.toInt64();
        if (d == 0) {
            return Value::makeError();
        }
        return Value(1.0 * a.toInt64() / d, VALUE_TYPE_DOUBLE);
    }
    if (b.int32Value == 0) {
        return Value::makeError();
    }
    return Value(1.0 * a.int32Value / b.int32Value, VALUE_TYPE_DOUBLE);
}
Value op_mod(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isDouble() || a.isFloat() || a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isDouble() || b.isFloat() || b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isDouble() || b.isDouble()) {
        return Value(a.toDouble() - floor(a.toDouble() / b.toDouble()) * b.toDouble(), VALUE_TYPE_DOUBLE);
    }
    if (a.isFloat() || b.isFloat()) {
        return Value(a.toFloat() - floor(a.toFloat() / b.toFloat()) * b.toFloat(), VALUE_TYPE_FLOAT);
    }
    if (a.isInt64() || b.isInt64()) {
        auto d = b.toInt64();
        if (d == 0) {
            return Value::makeError();
        }
        return Value(a.toInt64() % d, VALUE_TYPE_INT64);
    }
    if (b.int32Value == 0) {
        return Value::makeError();
    }
    return Value((int)(a.int32Value % b.int32Value), VALUE_TYPE_INT32);
}
Value op_left_shift(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isInt64() || b.isInt64()) {
        return Value(a.toInt64() << b.toInt64(), VALUE_TYPE_INT64);
    }
    return Value((int)(a.toInt32() << b.toInt32()), VALUE_TYPE_INT32);
}
Value op_right_shift(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isInt64() || b.isInt64()) {
        return Value(a.toInt64() >> b.toInt64(), VALUE_TYPE_INT64);
    }
    return Value((int)(a.toInt32() >> b.toInt32()), VALUE_TYPE_INT32);
}
Value op_binary_and(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isInt64() || b.isInt64()) {
        return Value(a.toInt64() & b.toInt64(), VALUE_TYPE_INT64);
    }
    return Value((int)(a.toInt32() & b.toInt32()), VALUE_TYPE_INT32);
}
Value op_binary_or(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isInt64() || b.isInt64()) {
        return Value(a.toInt64() | b.toInt64(), VALUE_TYPE_INT64);
    }
    return Value((int)(a.toInt32() | b.toInt32()), VALUE_TYPE_INT32);
}
Value op_binary_xor(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (!(a.isInt64() || a.isInt32OrLess())) {
        return Value::makeError();
    }
    if (!(b.isInt64() || b.isInt32OrLess())) {
        return Value::makeError();
    }
    if (a.isInt64() || b.isInt64()) {
        return Value(a.toInt64() ^ b.toInt64(), VALUE_TYPE_INT64);
    }
    return Value((int)(a.toInt32() ^ b.toInt32()), VALUE_TYPE_INT32);
}
bool is_equal(const Value& a1, const Value& b1) {
    auto a = a1.getValue();
    auto b = b1.getValue();
    auto aIsUndefinedOrNull = a.getType() == VALUE_TYPE_UNDEFINED || a.getType() == VALUE_TYPE_NULL;
    auto bIsUndefinedOrNull = b.getType() == VALUE_TYPE_UNDEFINED || b.getType() == VALUE_TYPE_NULL;
    if (aIsUndefinedOrNull) {
        return bIsUndefinedOrNull;
    } else if (bIsUndefinedOrNull) {
        return false;
    }
    if (a.isString() && b.isString()) {
        const char *aStr = a.getString();
        const char *bStr = b.getString();
        if (!aStr && !aStr) {
            return true;
        }
        if (!aStr || !bStr) {
            return false;
        }
        return strcmp(aStr, bStr) == 0;
    }
    if (a.isBlob() && b.isBlob()) {
        auto aBlobRef = a.getBlob();
        auto bBlobRef = b.getBlob();
        if (!aBlobRef && !aBlobRef) {
            return true;
        }
        if (!aBlobRef || !bBlobRef) {
            return false;
        }
        if (aBlobRef->len != bBlobRef->len) {
            return false;
        }
        return memcmp(aBlobRef->blob, bBlobRef->blob, aBlobRef->len) == 0;
    }
    return a.toDouble() == b.toDouble();
}
bool is_less(const Value& a1, const Value& b1) {
    auto a = a1.getValue();
    auto b = b1.getValue();
    if (a.isString() && b.isString()) {
        const char *aStr = a.getString();
        const char *bStr = b.getString();
        if (!aStr || !bStr) {
            return false;
        }
        return strcmp(aStr, bStr) < 0;
    }
    return a.toDouble() < b.toDouble();
}
bool is_great(const Value& a1, const Value& b1) {
    return !is_less(a1, b1) && !is_equal(a1, b1);
}
Value op_eq(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    return Value(is_equal(a1, b1), VALUE_TYPE_BOOLEAN);
}
Value op_neq(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    return Value(!is_equal(a1, b1), VALUE_TYPE_BOOLEAN);
}
Value op_less(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    return Value(is_less(a1, b1), VALUE_TYPE_BOOLEAN);
}
Value op_great(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    return Value(is_great(a1, b1), VALUE_TYPE_BOOLEAN);
}
Value op_less_eq(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    return Value(is_less(a1, b1) || is_equal(a1, b1), VALUE_TYPE_BOOLEAN);
}
Value op_great_eq(const Value& a1, const Value& b1) {
    if (a1.isError()) {
        return a1;
    }
    if (b1.isError()) {
        return b1;
    }
    return Value(!is_less(a1, b1), VALUE_TYPE_BOOLEAN);
}
void do_OPERATION_TYPE_ADD(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_add(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_SUB(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_sub(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_MUL(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_mul(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_DIV(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_div(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_MOD(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_mod(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_LEFT_SHIFT(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_left_shift(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_RIGHT_SHIFT(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_right_shift(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_BINARY_AND(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_binary_and(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_BINARY_OR(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_binary_or(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_BINARY_XOR(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    auto result = op_binary_xor(a, b);
    if (result.getType() == VALUE_TYPE_UNDEFINED) {
        result = Value::makeError();
    }
    stack.push(result);
}
void do_OPERATION_TYPE_EQUAL(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    stack.push(op_eq(a, b));
}
void do_OPERATION_TYPE_NOT_EQUAL(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    stack.push(op_neq(a, b));
}
void do_OPERATION_TYPE_LESS(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    stack.push(op_less(a, b));
}
void do_OPERATION_TYPE_GREATER(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    stack.push(op_great(a, b));
}
void do_OPERATION_TYPE_LESS_OR_EQUAL(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    stack.push(op_less_eq(a, b));
}
void do_OPERATION_TYPE_GREATER_OR_EQUAL(EvalStack &stack) {
    auto b = stack.pop();
    auto a = stack.pop();
    stack.push(op_great_eq(a, b));
}
void do_OPERATION_TYPE_LOGICAL_AND(EvalStack &stack) {
    auto bValue = stack.pop().getValue();
    auto aValue = stack.pop().getValue();
    if (aValue.isError()) {
        stack.push(aValue);
        return;
    }
    if (!aValue.toBool()) {
        stack.push(Value(false, VALUE_TYPE_BOOLEAN));
        return;
    }
    if (bValue.isError()) {
        stack.push(bValue);
        return;
    }
    stack.push(Value(bValue.toBool(), VALUE_TYPE_BOOLEAN));
}
void do_OPERATION_TYPE_LOGICAL_OR(EvalStack &stack) {
    auto bValue = stack.pop().getValue();
    auto aValue = stack.pop().getValue();
    if (aValue.isError()) {
        stack.push(aValue);
        return;
    }
    if (aValue.toBool()) {
        stack.push(Value(true, VALUE_TYPE_BOOLEAN));
        return;
    }
    if (bValue.isError()) {
        stack.push(bValue);
        return;
    }
    stack.push(Value(bValue.toBool(), VALUE_TYPE_BOOLEAN));
}
void do_OPERATION_TYPE_UNARY_PLUS(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isDouble()) {
        stack.push(Value(a.getDouble(), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(a.toFloat(), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt64()) {
        stack.push(Value((int64_t)a.getInt64(), VALUE_TYPE_INT64));
        return;
    }
    if (a.isInt32()) {
        stack.push(Value((int)a.getInt32(), VALUE_TYPE_INT32));
        return;
    }
    if (a.isInt16()) {
        stack.push(Value((int16_t)a.getInt16(), VALUE_TYPE_INT16));
        return;
    }
    if (a.isInt8()) {
        stack.push(Value((int8_t)a.getInt8(), VALUE_TYPE_INT8));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_UNARY_MINUS(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isDouble()) {
        stack.push(Value(-a.getDouble(), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(-a.toFloat(), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt64()) {
        stack.push(Value((int64_t)-a.getInt64(), VALUE_TYPE_INT64));
        return;
    }
    if (a.isInt32()) {
        stack.push(Value((int)-a.getInt32(), VALUE_TYPE_INT32));
        return;
    }
    if (a.isInt16()) {
        stack.push(Value((int16_t)-a.getInt16(), VALUE_TYPE_INT16));
        return;
    }
    if (a.isInt8()) {
        stack.push(Value((int8_t)-a.getInt8(), VALUE_TYPE_INT8));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_BINARY_ONE_COMPLEMENT(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isInt64()) {
        stack.push(Value(~a.uint64Value, VALUE_TYPE_UINT64));
        return;
    }
    if (a.isInt32()) {
        stack.push(Value(~a.uint32Value, VALUE_TYPE_UINT32));
        return;
    }
    if (a.isInt16()) {
        stack.push(Value(~a.uint16Value, VALUE_TYPE_UINT16));
        return;
    }
    if (a.isInt8()) {
        stack.push(Value(~a.uint8Value, VALUE_TYPE_UINT8));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_NOT(EvalStack &stack) {
    auto aValue = stack.pop();
    if (aValue.isError()) {
        stack.push(aValue);
        return;
    }
    int err;
    auto a = aValue.toBool(&err);
    if (err != 0) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(Value(!a, VALUE_TYPE_BOOLEAN));
}
void do_OPERATION_TYPE_CONDITIONAL(EvalStack &stack) {
    auto alternate = stack.pop();
    auto consequent = stack.pop();
    auto conditionValue = stack.pop();
    if (conditionValue.isError()) {
        stack.push(conditionValue);
        return;
    }
    int err;
    auto condition = conditionValue.toBool(&err);
    if (err != 0) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(condition ? consequent : alternate);
}
void do_OPERATION_TYPE_SYSTEM_GET_TICK(EvalStack &stack) {
    stack.push(Value(millis(), VALUE_TYPE_UINT32));
}
void do_OPERATION_TYPE_FLOW_INDEX(EvalStack &stack) {
    if (!stack.iterators) {
        stack.push(Value::makeError());
        return;
    }
    auto a = stack.pop();
    int err;
    auto iteratorIndex = a.toInt32(&err);
    if (err != 0) {
        stack.push(Value::makeError());
        return;
    }
    iteratorIndex = iteratorIndex;
    if (iteratorIndex < 0 || iteratorIndex >= (int)MAX_ITERATORS) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(stack.iterators[iteratorIndex]);
}
void do_OPERATION_TYPE_FLOW_IS_PAGE_ACTIVE(EvalStack &stack) {
#if EEZ_OPTION_GUI
    bool isActive = false;
    auto pageIndex = getPageIndex(stack.flowState);
    if (pageIndex >= 0) {
        int16_t pageId = (int16_t)(pageIndex + 1);
        if (stack.flowState->assets == g_externalAssets) {
            pageId = -pageId;
        }
        for (int16_t appContextId = 0; ; appContextId++) {
            auto appContext = getAppContextFromId(appContextId);
            if (!appContext) {
                break;
            }
            if (appContext->isPageOnStack(pageId)) {
                isActive = true;
                break;
            }
        }
    }
    stack.push(Value(isActive, VALUE_TYPE_BOOLEAN));
#elif defined(EEZ_FOR_LVGL)
    auto pageIndex = getPageIndex(stack.flowState);
    stack.push(Value(pageIndex == g_currentScreen, VALUE_TYPE_BOOLEAN));
#else
    stack.push(Value::makeError());
#endif 
}
void do_OPERATION_TYPE_FLOW_PAGE_TIMELINE_POSITION(EvalStack &stack) {
    stack.push(Value(stack.flowState->timelinePosition, VALUE_TYPE_FLOAT));
}
void do_OPERATION_TYPE_FLOW_MAKE_ARRAY_VALUE(EvalStack &stack) {
    auto arrayTypeValue = stack.pop();
    if (arrayTypeValue.isError()) {
        stack.push(arrayTypeValue);
        return;
    }
    auto arraySizeValue = stack.pop();
    if (arraySizeValue.isError()) {
        stack.push(arraySizeValue);
        return;
    }
    auto numInitElementsValue = stack.pop();
    if (numInitElementsValue.isError()) {
        stack.push(numInitElementsValue);
        return;
    }
    int arrayType = arrayTypeValue.getInt();
    int err;
    int arraySize = arraySizeValue.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    int numInitElements = numInitElementsValue.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
#if defined(EEZ_DASHBOARD_API)
    if (arrayType == VALUE_TYPE_JSON) {
        Value jsonValue = operationJsonMake();
        for (int i = 0; i < arraySize; i += 2) {
            Value propertyName = stack.pop().getValue();
            if (!propertyName.isString()) {
                stack.push(Value::makeError());
                return;
            }
            Value propertyValue = stack.pop().getValue();
            if (propertyValue.isError()) {
                stack.push(propertyValue);
                return;
            }
            operationJsonSet(jsonValue.getInt(), propertyName.getString(), &propertyValue);
        }
        stack.push(jsonValue);
        return;
    }
#endif
    auto arrayValue = Value::makeArrayRef(arraySize, arrayType, 0x837260d4);
    auto array = arrayValue.getArray();
    for (int i = 0; i < arraySize; i++) {
        if (i < numInitElements) {
            array->values[i] = stack.pop().getValue();
        } else {
            array->values[i] = Value();
        }
    }
    stack.push(arrayValue);
}
void do_OPERATION_TYPE_FLOW_LANGUAGES(EvalStack &stack) {
    auto &languages = stack.flowState->assets->languages;
    auto arrayValue = Value::makeArrayRef(languages.count, VALUE_TYPE_STRING, 0xff4787fc);
    auto array = arrayValue.getArray();
    for (uint32_t i = 0; i < languages.count; i++) {
        array->values[i] = Value((const char *)(languages[i]->languageID));
    }
    stack.push(arrayValue);
}
void do_OPERATION_TYPE_FLOW_TRANSLATE(EvalStack &stack) {
    auto textResourceIndexValue = stack.pop();
    int err;
    int textResourceIndex = textResourceIndexValue.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    int languageIndex = g_selectedLanguage;
    auto &languages = stack.flowState->assets->languages;
    if (languageIndex >= 0 && languageIndex < (int)languages.count) {
        auto &translations = languages[languageIndex]->translations;
        if (textResourceIndex >= 0 && textResourceIndex < (int)translations.count) {
            stack.push(translations[textResourceIndex]);
            return;
        }
    }
    stack.push("");
}
void do_OPERATION_TYPE_FLOW_PARSE_INTEGER(EvalStack &stack) {
    auto str = stack.pop();
    if (str.isError()) {
        stack.push(str);
        return;
    }
    int err;
    auto value = str.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(Value((int)value, VALUE_TYPE_INT32));
}
void do_OPERATION_TYPE_FLOW_PARSE_FLOAT(EvalStack &stack) {
    auto str = stack.pop();
    if (str.isError()) {
        stack.push(str);
        return;
    }
    int err;
    auto value = str.toFloat(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(Value(value, VALUE_TYPE_FLOAT));
}
void do_OPERATION_TYPE_FLOW_PARSE_DOUBLE(EvalStack &stack) {
    auto str = stack.pop();
    if (str.isError()) {
        stack.push(str);
        return;
    }
    int err;
    auto value = str.toDouble(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(Value(value, VALUE_TYPE_DOUBLE));
}
void do_OPERATION_TYPE_FLOW_TO_INTEGER(EvalStack &stack) {
    auto str = stack.pop();
    if (str.isError()) {
        stack.push(str);
        return;
    }
    int err;
    int value = str.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(Value(value, VALUE_TYPE_INT32));
}
void do_OPERATION_TYPE_FLOW_GET_BITMAP_INDEX(EvalStack &stack) {
#if EEZ_OPTION_GUI
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    Value bitmapName = a.toString(0x244c1880);
    int bitmapId = getBitmapIdByName(bitmapName.getString());
    stack.push(Value(bitmapId, VALUE_TYPE_INT32));
#else
    stack.push(Value::makeError());
#endif 
}
void do_OPERATION_TYPE_FLOW_GET_BITMAP_AS_DATA_URL(EvalStack &stack) {
#if defined(EEZ_DASHBOARD_API)
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    Value bitmapName = a.toString(0xcdc34cc3);
    stack.push(getBitmapAsDataURL(bitmapName.getString()));
#else
    stack.push(Value::makeError());
#endif 
}
void do_OPERATION_TYPE_DATE_NOW(EvalStack &stack) {
    stack.push(Value((double)date::now(), VALUE_TYPE_DATE));
}
void do_OPERATION_TYPE_DATE_TO_STRING(EvalStack &stack) {
#ifndef ARDUINO
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    char str[128];
    date::toString(a.getDouble(), str, sizeof(str));
    stack.push(Value::makeStringRef(str, -1, 0xbe440ec8));
#else
    stack.push(Value::makeError());
#endif
}
void do_OPERATION_TYPE_DATE_TO_LOCALE_STRING(EvalStack &stack) {
#ifndef ARDUINO
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    char str[128];
    date::toLocaleString(a.getDouble(), str, sizeof(str));
    stack.push(Value::makeStringRef(str, -1, 0xbe440ec8));
#else
    stack.push(Value::makeError());
#endif
}
void do_OPERATION_TYPE_DATE_FROM_STRING(EvalStack &stack) {
#ifndef ARDUINO
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    Value dateStrValue = a.toString(0x99cb1a93);
    auto date = (double)date::fromString(dateStrValue.getString());
    stack.push(Value(date, VALUE_TYPE_DATE));
#else
    stack.push(Value::makeError());
#endif
}
void do_OPERATION_TYPE_DATE_GET_YEAR(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(date::getYear(a.getDouble()));
}
void do_OPERATION_TYPE_DATE_GET_MONTH(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(date::getMonth(a.getDouble()));
}
void do_OPERATION_TYPE_DATE_GET_DAY(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(date::getDay(a.getDouble()));
}
void do_OPERATION_TYPE_DATE_GET_HOURS(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(date::getHours(a.getDouble()));
}
void do_OPERATION_TYPE_DATE_GET_MINUTES(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(date::getMinutes(a.getDouble()));
}
void do_OPERATION_TYPE_DATE_GET_SECONDS(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(date::getSeconds(a.getDouble()));
}
void do_OPERATION_TYPE_DATE_GET_MILLISECONDS(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.getType() != VALUE_TYPE_DATE) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(date::getMilliseconds(a.getDouble()));
}
void do_OPERATION_TYPE_DATE_MAKE(EvalStack &stack) {
    int err;
    Value value;
    value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    int year = value.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    int month = value.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    int day = value.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    int hours = value.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    int minutes = value.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    int seconds = value.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    int milliseconds = value.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    auto date = (double)date::makeDate(year, month, day, hours, minutes, seconds, milliseconds);
    stack.push(Value(date, VALUE_TYPE_DATE));
}
void do_OPERATION_TYPE_MATH_SIN(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.isDouble()) {
        stack.push(Value(sin(a.getDouble()), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(sinf(a.toFloat()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt64()) {
        stack.push(Value(sin(a.toInt64()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt32OrLess()) {
        stack.push(Value(sinf(a.int32Value), VALUE_TYPE_FLOAT));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_MATH_COS(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.isDouble()) {
        stack.push(Value(cos(a.getDouble()), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(cosf(a.toFloat()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt64()) {
        stack.push(Value(cos(a.toInt64()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt32OrLess()) {
        stack.push(Value(cosf(a.int32Value), VALUE_TYPE_FLOAT));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_MATH_POW(EvalStack &stack) {
    auto baseValue = stack.pop().getValue();
    if (baseValue.isError()) {
        stack.push(Value::makeError());
        return;
    }
    if (!baseValue.isInt32OrLess() && !baseValue.isFloat() && !baseValue.isDouble()) {
        stack.push(Value::makeError());
        return;
    }
    auto exponentValue = stack.pop().getValue();
    if (exponentValue.isError()) {
        stack.push(Value::makeError());
        return;
    }
    if (!exponentValue.isInt32OrLess() && !exponentValue.isFloat() && !exponentValue.isDouble()) {
        stack.push(Value::makeError());
        return;
    }
    if (baseValue.isFloat() && (exponentValue.isFloat() || exponentValue.isInt32OrLess())) {
        int err;
        float base = baseValue.toFloat(&err);
        if (err) {
            stack.push(Value::makeError());
            return;
        }
        float exponent = exponentValue.toFloat(&err);
        if (err) {
            stack.push(Value::makeError());
            return;
        }
        float result = powf(base, exponent);
        stack.push(Value(result, VALUE_TYPE_FLOAT));
    } else {
        int err;
        double base = baseValue.toDouble(&err);
        if (err) {
            stack.push(Value::makeError());
            return;
        }
        double exponent = exponentValue.toDouble(&err);
        if (err) {
            stack.push(Value::makeError());
            return;
        }
        double result = pow(base, exponent);
        stack.push(Value(result, VALUE_TYPE_DOUBLE));
    }
}
void do_OPERATION_TYPE_MATH_LOG(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.isDouble()) {
        stack.push(Value(log(a.getDouble()), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(logf(a.toFloat()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt64()) {
        stack.push(Value(log(a.toInt64()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt32OrLess()) {
        stack.push(Value(logf(a.int32Value), VALUE_TYPE_FLOAT));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_MATH_LOG10(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.isDouble()) {
        stack.push(Value(log10(a.getDouble()), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(log10f(a.toFloat()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt64()) {
        stack.push(Value(log10(a.toInt64()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt32OrLess()) {
        stack.push(Value(log10f(a.int32Value), VALUE_TYPE_FLOAT));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_MATH_ABS(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.isDouble()) {
        stack.push(Value(abs(a.getDouble()), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(abs(a.toFloat()), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt64()) {
        stack.push(Value((int64_t)abs(a.getInt64()), VALUE_TYPE_INT64));
        return;
    }
    if (a.isInt32()) {
        stack.push(Value((int)abs(a.getInt32()), VALUE_TYPE_INT32));
        return;
    }
    if (a.isInt16()) {
        stack.push(Value(abs(a.getInt16()), VALUE_TYPE_INT16));
        return;
    }
    if (a.isInt8()) {
        stack.push(Value(abs(a.getInt8()), VALUE_TYPE_INT8));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_MATH_FLOOR(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.isInt32OrLess()) {
        stack.push(a);
        return;
    }
    if (a.isDouble()) {
        stack.push(Value(floor(a.getDouble()), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(floorf(a.toFloat()), VALUE_TYPE_FLOAT));
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_MATH_CEIL(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.isInt32OrLess()) {
        stack.push(a);
        return;
    }
    if (a.isDouble()) {
        stack.push(Value(ceil(a.getDouble()), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(ceilf(a.toFloat()), VALUE_TYPE_FLOAT));
        return;
    }
    stack.push(Value::makeError());
}
float roundN(float value, unsigned int numDigits) {
  float pow_10 = pow(10.0f, numDigits);
  return round(value * pow_10) / pow_10;
}
double roundN(double value, unsigned int numDigits) {
  float pow_10 = pow(10.0f, numDigits);
  return round(value * pow_10) / pow_10;
}
void do_OPERATION_TYPE_MATH_ROUND(EvalStack &stack) {
    auto numArgs = stack.pop().getInt();
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    unsigned int numDigits;
    if (numArgs > 1) {
        auto b = stack.pop().getValue();
        numDigits = b.toInt32();
    } else {
        numDigits = 0;
    }
    if (a.isInt32OrLess()) {
        stack.push(a);
        return;
    }
    if (a.isDouble()) {
        stack.push(Value(roundN(a.getDouble(), numDigits), VALUE_TYPE_DOUBLE));
        return;
    }
    if (a.isFloat()) {
        stack.push(Value(roundN(a.toFloat(), numDigits), VALUE_TYPE_FLOAT));
        return;
    }
    if (a.isInt32OrLess()) {
        stack.push(a);
        return;
    }
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_MATH_MIN(EvalStack &stack) {
    auto numArgs = stack.pop().getInt();
    Value minValue;
    for (int i = 0; i < numArgs; i++) {
        auto value = stack.pop().getValue();
        if (value.isError()) {
            stack.push(value);
            return;
        }
        if (minValue.isUndefinedOrNull() || is_less(value, minValue)) {
            minValue = value;
        }
    }
    stack.push(minValue);
}
void do_OPERATION_TYPE_MATH_MAX(EvalStack &stack) {
    auto numArgs = stack.pop().getInt();
    Value maxValue;
    for (int i = 0; i < numArgs; i++) {
        auto value = stack.pop().getValue();
        if (value.isError()) {
            stack.push(value);
            return;
        }
        if (maxValue.isUndefinedOrNull() || is_great(value, maxValue)) {
            maxValue = value;
        }
    }
    stack.push(maxValue);
}
void do_OPERATION_TYPE_STRING_LENGTH(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    const char *aStr = a.getString();
    if (!aStr) {
        stack.push(Value::makeError());
        return;
    }
    int aStrLen = strlen(aStr);
    stack.push(Value(aStrLen, VALUE_TYPE_INT32));
}
void do_OPERATION_TYPE_STRING_SUBSTRING(EvalStack &stack) {
    auto numArgs = stack.pop().getInt();
    Value strValue = stack.pop().getValue();
    if (strValue.isError()) {
        stack.push(strValue);
        return;
    }
    Value startValue = stack.pop().getValue();
    if (startValue.isError()) {
        stack.push(startValue);
        return;
    }
    Value endValue;
    if (numArgs == 3) {
        endValue = stack.pop().getValue();
        if (endValue.isError()) {
            stack.push(endValue);
            return;
        }
    }
    const char *str = strValue.getString();
    if (!str) {
        stack.push(Value::makeError());
        return;
    }
    int strLen = (int)strlen(str);
    int err = 0;
    int start = startValue.toInt32(&err);
    if (err != 0) {
        stack.push(Value::makeError());
        return;
    }
    int end;
    if (endValue.getType() == VALUE_TYPE_UNDEFINED) {
        end = strLen;
    } else {
        end = endValue.toInt32(&err);
        if (err != 0) {
            stack.push(Value::makeError());
            return;
        }
    }
    if (start < 0) {
        start = 0;
    } else if (start > strLen) {
        start = strLen;
    }
    if (end < 0) {
        end = 0;
    } else if (end > strLen) {
        end = strLen;
    }
    if (start < end) {
        Value resultValue = Value::makeStringRef(str + start, end - start, 0x203b08a2);
        stack.push(resultValue);
        return;
    }
    stack.push(Value("", VALUE_TYPE_STRING));
}
void do_OPERATION_TYPE_STRING_FIND(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    auto b = stack.pop().getValue();
    if (b.isError()) {
        stack.push(b);
        return;
    }
    Value aStr = a.toString(0xf616bf4d);
    Value bStr = b.toString(0x81229133);
    if (!aStr.getString() || !bStr.getString()) {
        stack.push(Value(-1, VALUE_TYPE_INT32));
        return;
    }
    const char *pos = strstr(aStr.getString(), bStr.getString());
    if (pos) {
        stack.push(Value((int)(pos - aStr.getString()), VALUE_TYPE_INT32));
        return;
    }
    stack.push(Value(-1, VALUE_TYPE_INT32));
}
void do_OPERATION_TYPE_STRING_PAD_START(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    auto b = stack.pop().getValue();
    if (b.isError()) {
        stack.push(b);
        return;
    }
    auto c = stack.pop().getValue();
    if (c.isError()) {
        stack.push(c);
        return;
    }
    auto str = a.toString(0xcf6aabe6);
    if (!str.getString()) {
        stack.push(Value::makeError());
        return;
    }
    int strLen = strlen(str.getString());
    int err;
    int targetLength = b.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    if (targetLength < strLen) {
        targetLength = strLen;
    }
    auto padStr = c.toString(0x81353bd7);
    if (!padStr.getString()) {
        stack.push(Value::makeError());
        return;
    }
    int padStrLen = strlen(padStr.getString());
    Value resultValue = Value::makeStringRef("", targetLength, 0xf43b14dd);
    if (resultValue.type == VALUE_TYPE_NULL) {
        stack.push(Value::makeError());
        return;
    }
    char *resultStr = (char *)resultValue.getString();
    auto n = targetLength - strLen;
    stringCopy(resultStr + (targetLength - strLen), strLen + 1, str.getString());
    for (int i = 0; i < n; i++) {
        resultStr[i] = padStr.getString()[i % padStrLen];
    }
    stack.push(resultValue);
}
void do_OPERATION_TYPE_STRING_SPLIT(EvalStack &stack) {
    auto strValue = stack.pop().getValue();
    if (strValue.isError()) {
        stack.push(strValue);
        return;
    }
    auto delimValue = stack.pop().getValue();
    if (delimValue.isError()) {
        stack.push(delimValue);
        return;
    }
    auto str = strValue.getString();
    if (!str) {
        stack.push(Value::makeError());
        return;
    }
    auto delim = delimValue.getString();
    if (!delim) {
        stack.push(Value::makeError());
        return;
    }
    auto strLen = strlen(str);
    char *strCopy = (char *)eez::alloc(strLen + 1, 0xea9d0bc0);
    stringCopy(strCopy, strLen + 1, str);
    size_t arraySize = 0;
    char *token = strtok(strCopy, delim);
    while (token != NULL) {
        arraySize++;
        token = strtok(NULL, delim);
    }
    eez::free(strCopy);
    strCopy = (char *)eez::alloc(strLen + 1, 0xea9d0bc1);
    stringCopy(strCopy, strLen + 1, str);
    auto arrayValue = Value::makeArrayRef(arraySize, VALUE_TYPE_STRING, 0xe82675d4);
    auto array = arrayValue.getArray();
    int i = 0;
    token = strtok(strCopy, delim);
    while (token != NULL) {
        array->values[i++] = Value::makeStringRef(token, -1, 0x45209ec0);
        token = strtok(NULL, delim);
    }
    eez::free(strCopy);
    stack.push(arrayValue);
}
void do_OPERATION_TYPE_STRING_FROM_CODE_POINT(EvalStack &stack) {
    Value charCodeValue = stack.pop().getValue();
    if (charCodeValue.isError()) {
        stack.push(charCodeValue);
        return;
    }
    int err = 0;
    int32_t charCode = charCodeValue.toInt32(&err);
    if (err != 0) {
        stack.push(Value::makeError());
        return;
    }
    char str[16] = {0};
    utf8catcodepoint(str, charCode, sizeof(str));
    Value resultValue = Value::makeStringRef(str, strlen(str), 0x02c2e778);
    stack.push(resultValue);
    return;
}
void do_OPERATION_TYPE_STRING_CODE_POINT_AT(EvalStack &stack) {
    auto strValue = stack.pop().getValue();
    if (strValue.isError()) {
        stack.push(strValue);
        return;
    }
    Value indexValue = stack.pop().getValue();
    if (indexValue.isError()) {
        stack.push(indexValue);
        return;
    }
    utf8_int32_t codePoint = 0;
    const char *str = strValue.getString();
    if (str) {
        int index = indexValue.toInt32();
        if (index >= 0) {
            do {
                str = utf8codepoint(str, &codePoint);
            } while (codePoint && --index >= 0);
        }
    }
    stack.push(Value((int)codePoint, VALUE_TYPE_INT32));
    return;
}
void do_OPERATION_TYPE_ARRAY_LENGTH(EvalStack &stack) {
    auto a = stack.pop().getValue();
    if (a.isError()) {
        stack.push(a);
        return;
    }
    if (a.isArray()) {
        auto array = a.getArray();
        stack.push(Value(array->arraySize, VALUE_TYPE_UINT32));
        return;
    }
    if (a.isBlob()) {
        auto blobRef = a.getBlob();
        stack.push(Value(blobRef->len, VALUE_TYPE_UINT32));
        return;
    }
#if defined(EEZ_DASHBOARD_API)
    if (a.isJson()) {
        int length = operationJsonArrayLength(a.getInt());
        if (length >= 0) {
            stack.push(Value(length, VALUE_TYPE_UINT32));
            return;
        }
    }
#endif
    stack.push(Value::makeError());
}
void do_OPERATION_TYPE_ARRAY_SLICE(EvalStack &stack) {
    auto numArgs = stack.pop().getInt();
    auto arrayValue = stack.pop().getValue();
    if (arrayValue.isError()) {
        stack.push(arrayValue);
        return;
    }
    int from = 0;
    if (numArgs > 1) {
        auto fromValue = stack.pop().getValue();
        if (fromValue.isError()) {
            stack.push(fromValue);
            return;
        }
        int err;
        from = fromValue.toInt32(&err);
        if (err) {
            stack.push(Value::makeError());
            return;
        }
        if (from < 0) {
            from = 0;
        }
    }
    int to = -1;
    if (numArgs > 2) {
        auto toValue = stack.pop().getValue();
        if (toValue.isError()) {
            stack.push(toValue);
            return;
        }
        int err;
        to = toValue.toInt32(&err);
        if (err) {
            stack.push(Value::makeError());
            return;
        }
        if (to < 0) {
            to = 0;
        }
    }
#if defined(EEZ_DASHBOARD_API)
    if (arrayValue.isJson()) {
        stack.push(operationJsonArraySlice(arrayValue.getInt(), from, to));
        return;
    }
#endif
    if (!arrayValue.isArray()) {
        stack.push(Value::makeError());
        return;
    }
    auto array = arrayValue.getArray();
    if (to == -1) {
        to = array->arraySize;
    }
    if (from > to) {
        stack.push(Value::makeError());
        return;
    }
    auto size = to - from;
    auto resultArrayValue = Value::makeArrayRef(size, array->arrayType, 0xe2d78c65);
    auto resultArray = resultArrayValue.getArray();
    for (int elementIndex = from; elementIndex < to && elementIndex < (int)array->arraySize; elementIndex++) {
        resultArray->values[elementIndex - from] = array->values[elementIndex];
    }
    stack.push(resultArrayValue);
}
void do_OPERATION_TYPE_ARRAY_ALLOCATE(EvalStack &stack) {
    auto sizeValue = stack.pop();
    if (sizeValue.isError()) {
        stack.push(sizeValue);
        return;
    }
    int err;
    auto size = sizeValue.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    auto resultArrayValue = Value::makeArrayRef(size, defs_v3::ARRAY_TYPE_ANY, 0xe2d78c65);
    stack.push(resultArrayValue);
}
void do_OPERATION_TYPE_ARRAY_APPEND(EvalStack &stack) {
    auto arrayValue = stack.pop().getValue();
    if (arrayValue.isError()) {
        stack.push(arrayValue);
        return;
    }
    auto value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
#if defined(EEZ_DASHBOARD_API)
    if (arrayValue.isJson()) {
        stack.push(operationJsonArrayAppend(arrayValue.getInt(), &value));
        return;
    }
#endif
    if (!arrayValue.isArray()) {
        stack.push(Value::makeError());
        return;
    }
    auto array = arrayValue.getArray();
    auto resultArrayValue = Value::makeArrayRef(array->arraySize + 1, array->arrayType, 0x664c3199);
    auto resultArray = resultArrayValue.getArray();
    for (uint32_t elementIndex = 0; elementIndex < array->arraySize; elementIndex++) {
        resultArray->values[elementIndex] = array->values[elementIndex];
    }
    resultArray->values[array->arraySize] = value;
    stack.push(resultArrayValue);
}
void do_OPERATION_TYPE_ARRAY_INSERT(EvalStack &stack) {
    auto arrayValue = stack.pop().getValue();
    if (arrayValue.isError()) {
        stack.push(arrayValue);
        return;
    }
    auto positionValue = stack.pop().getValue();
    if (positionValue.isError()) {
        stack.push(positionValue);
        return;
    }
    auto value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    int err;
    auto position = positionValue.toInt32(&err);
    if (err != 0) {
        stack.push(Value::makeError());
        return;
    }
#if defined(EEZ_DASHBOARD_API)
    if (arrayValue.isJson()) {
        stack.push(operationJsonArrayInsert(arrayValue.getInt(), position, &value));
        return;
    }
#endif
    if (!arrayValue.isArray()) {
        stack.push(Value::makeError());
        return;
    }
    auto array = arrayValue.getArray();
    auto resultArrayValue = Value::makeArrayRef(array->arraySize + 1, array->arrayType, 0xc4fa9cd9);
    auto resultArray = resultArrayValue.getArray();
    if (position < 0) {
        position = 0;
    } else if (position > array->arraySize) {
        position = array->arraySize;
    }
    for (uint32_t elementIndex = 0; (int)elementIndex < position; elementIndex++) {
        resultArray->values[elementIndex] = array->values[elementIndex];
    }
    resultArray->values[position] = value;
    for (uint32_t elementIndex = position; elementIndex < array->arraySize; elementIndex++) {
        resultArray->values[elementIndex + 1] = array->values[elementIndex];
    }
    stack.push(resultArrayValue);
}
void do_OPERATION_TYPE_ARRAY_REMOVE(EvalStack &stack) {
    auto arrayValue = stack.pop().getValue();
    if (arrayValue.isError()) {
        stack.push(arrayValue);
        return;
    }
    auto positionValue = stack.pop().getValue();
    if (positionValue.isError()) {
        stack.push(positionValue);
        return;
    }
    int err;
    auto position = positionValue.toInt32(&err);
    if (err != 0) {
        stack.push(Value::makeError());
        return;
    }
#if defined(EEZ_DASHBOARD_API)
    if (arrayValue.isJson()) {
        stack.push(operationJsonArrayRemove(arrayValue.getInt(), position));
        return;
    }
#endif
    if (!arrayValue.isArray()) {
        stack.push(Value::makeError());
        return;
    }
    auto array = arrayValue.getArray();
    if (position >= 0 && position < (int32_t)array->arraySize) {
        auto resultArrayValue = Value::makeArrayRef(array->arraySize - 1, array->arrayType, 0x40e9bb4b);
        auto resultArray = resultArrayValue.getArray();
        for (uint32_t elementIndex = 0; (int)elementIndex < position; elementIndex++) {
            resultArray->values[elementIndex] = array->values[elementIndex];
        }
        for (uint32_t elementIndex = position + 1; elementIndex < array->arraySize; elementIndex++) {
            resultArray->values[elementIndex - 1] = array->values[elementIndex];
        }
        stack.push(resultArrayValue);
    } else {
        stack.push(Value::makeError());
    }
}
void do_OPERATION_TYPE_ARRAY_CLONE(EvalStack &stack) {
    auto arrayValue = stack.pop().getValue();
    if (arrayValue.isError()) {
        stack.push(arrayValue);
        return;
    }
    auto resultArray = arrayValue.clone();
    stack.push(resultArray);
}
void do_OPERATION_TYPE_LVGL_METER_TICK_INDEX(EvalStack &stack) {
    stack.push(g_eezFlowLvlgMeterTickIndex);
}
void do_OPERATION_TYPE_CRYPTO_SHA256(EvalStack &stack) {
    auto value = stack.pop().getValue();
    if (value.isError()) {
        stack.push(value);
        return;
    }
    const uint8_t *data;
    uint32_t dataLen;
    if (value.isString()) {
        const char *str = value.getString();
        data = (uint8_t *)str;
        dataLen = strlen(str);
    } else if (value.isBlob()) {
        auto blobRef = value.getBlob();
        data = blobRef->blob;
        dataLen = blobRef->len;
    } else {
        stack.push(Value::makeError());
        return;
    }
#if EEZ_FOR_LVGL_SHA256_OPTION
    BYTE buf[SHA256_BLOCK_SIZE];
    SHA256_CTX ctx;
    sha256_init(&ctx);
	sha256_update(&ctx, data, dataLen);
	sha256_final(&ctx, buf);
    auto result = Value::makeBlobRef(buf, SHA256_BLOCK_SIZE, 0x1f0c0c0c);
    stack.push(result);
#else
    stack.push(Value::makeError());
#endif
}
void do_OPERATION_TYPE_BLOB_ALLOCATE(EvalStack &stack) {
    auto sizeValue = stack.pop();
    if (sizeValue.isError()) {
        stack.push(sizeValue);
        return;
    }
    int err;
    auto size = sizeValue.toInt32(&err);
    if (err) {
        stack.push(Value::makeError());
        return;
    }
    auto result = Value::makeBlobRef(nullptr, size, 0xd3de43f1);
    stack.push(result);
}
void do_OPERATION_TYPE_JSON_GET(EvalStack &stack) {
#if defined(EEZ_DASHBOARD_API)
    auto jsonValue = stack.pop().getValue();
    auto propertyValue = stack.pop();
    if (jsonValue.isError()) {
        stack.push(jsonValue);
        return;
    }
    if (jsonValue.type != VALUE_TYPE_JSON) {
        stack.push(Value::makeError());
        return;
    }
    if (propertyValue.isError()) {
        stack.push(propertyValue);
        return;
    }
    stack.push(Value::makeJsonMemberRef(jsonValue, propertyValue.toString(0xc73d02e7), 0xebcc230a));
#else
    stack.push(Value::makeError());
#endif
}
void do_OPERATION_TYPE_JSON_CLONE(EvalStack &stack) {
#if defined(EEZ_DASHBOARD_API)
    auto jsonValue = stack.pop().getValue();
    if (jsonValue.isError()) {
        stack.push(jsonValue);
        return;
    }
    if (jsonValue.type != VALUE_TYPE_JSON) {
        stack.push(Value::makeError());
        return;
    }
    stack.push(operationJsonClone(jsonValue.getInt()));
#else
    stack.push(Value::makeError());
#endif
}
EvalOperation g_evalOperations[] = {
    do_OPERATION_TYPE_ADD,
    do_OPERATION_TYPE_SUB,
    do_OPERATION_TYPE_MUL,
    do_OPERATION_TYPE_DIV,
    do_OPERATION_TYPE_MOD,
    do_OPERATION_TYPE_LEFT_SHIFT,
    do_OPERATION_TYPE_RIGHT_SHIFT,
    do_OPERATION_TYPE_BINARY_AND,
    do_OPERATION_TYPE_BINARY_OR,
    do_OPERATION_TYPE_BINARY_XOR,
    do_OPERATION_TYPE_EQUAL,
    do_OPERATION_TYPE_NOT_EQUAL,
    do_OPERATION_TYPE_LESS,
    do_OPERATION_TYPE_GREATER,
    do_OPERATION_TYPE_LESS_OR_EQUAL,
    do_OPERATION_TYPE_GREATER_OR_EQUAL,
    do_OPERATION_TYPE_LOGICAL_AND,
    do_OPERATION_TYPE_LOGICAL_OR,
    do_OPERATION_TYPE_UNARY_PLUS,
    do_OPERATION_TYPE_UNARY_MINUS,
    do_OPERATION_TYPE_BINARY_ONE_COMPLEMENT,
    do_OPERATION_TYPE_NOT,
    do_OPERATION_TYPE_CONDITIONAL,
    do_OPERATION_TYPE_SYSTEM_GET_TICK,
    do_OPERATION_TYPE_FLOW_INDEX,
    do_OPERATION_TYPE_FLOW_IS_PAGE_ACTIVE,
    do_OPERATION_TYPE_FLOW_PAGE_TIMELINE_POSITION,
    do_OPERATION_TYPE_FLOW_MAKE_ARRAY_VALUE,
    do_OPERATION_TYPE_FLOW_MAKE_ARRAY_VALUE,
    do_OPERATION_TYPE_FLOW_LANGUAGES,
    do_OPERATION_TYPE_FLOW_TRANSLATE,
    do_OPERATION_TYPE_FLOW_PARSE_INTEGER,
    do_OPERATION_TYPE_FLOW_PARSE_FLOAT,
    do_OPERATION_TYPE_FLOW_PARSE_DOUBLE,
    do_OPERATION_TYPE_DATE_NOW,
    do_OPERATION_TYPE_DATE_TO_STRING,
    do_OPERATION_TYPE_DATE_FROM_STRING,
    do_OPERATION_TYPE_MATH_SIN,
    do_OPERATION_TYPE_MATH_COS,
    do_OPERATION_TYPE_MATH_LOG,
    do_OPERATION_TYPE_MATH_LOG10,
    do_OPERATION_TYPE_MATH_ABS,
    do_OPERATION_TYPE_MATH_FLOOR,
    do_OPERATION_TYPE_MATH_CEIL,
    do_OPERATION_TYPE_MATH_ROUND,
    do_OPERATION_TYPE_MATH_MIN,
    do_OPERATION_TYPE_MATH_MAX,
    do_OPERATION_TYPE_STRING_LENGTH,
    do_OPERATION_TYPE_STRING_SUBSTRING,
    do_OPERATION_TYPE_STRING_FIND,
    do_OPERATION_TYPE_STRING_PAD_START,
    do_OPERATION_TYPE_STRING_SPLIT,
    do_OPERATION_TYPE_ARRAY_LENGTH,
    do_OPERATION_TYPE_ARRAY_SLICE,
    do_OPERATION_TYPE_ARRAY_ALLOCATE,
    do_OPERATION_TYPE_ARRAY_APPEND,
    do_OPERATION_TYPE_ARRAY_INSERT,
    do_OPERATION_TYPE_ARRAY_REMOVE,
    do_OPERATION_TYPE_ARRAY_CLONE,
    do_OPERATION_TYPE_DATE_TO_LOCALE_STRING,
    do_OPERATION_TYPE_DATE_GET_YEAR,
    do_OPERATION_TYPE_DATE_GET_MONTH,
    do_OPERATION_TYPE_DATE_GET_DAY,
    do_OPERATION_TYPE_DATE_GET_HOURS,
    do_OPERATION_TYPE_DATE_GET_MINUTES,
    do_OPERATION_TYPE_DATE_GET_SECONDS,
    do_OPERATION_TYPE_DATE_GET_MILLISECONDS,
    do_OPERATION_TYPE_DATE_MAKE,
    do_OPERATION_TYPE_MATH_POW,
    do_OPERATION_TYPE_LVGL_METER_TICK_INDEX,
    do_OPERATION_TYPE_FLOW_GET_BITMAP_INDEX,
    do_OPERATION_TYPE_FLOW_TO_INTEGER,
    do_OPERATION_TYPE_STRING_FROM_CODE_POINT,
    do_OPERATION_TYPE_STRING_CODE_POINT_AT,
    do_OPERATION_TYPE_CRYPTO_SHA256,
    do_OPERATION_TYPE_BLOB_ALLOCATE,
    do_OPERATION_TYPE_JSON_GET,
    do_OPERATION_TYPE_JSON_CLONE,
    do_OPERATION_TYPE_FLOW_GET_BITMAP_AS_DATA_URL,
};
} 
} 
// -----------------------------------------------------------------------------
// flow/private.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#if EEZ_OPTION_GUI
using namespace eez::gui;
#endif
#if defined(EEZ_DASHBOARD_API)
#endif
namespace eez {
namespace flow {
GlobalVariables *g_globalVariables = nullptr;
static const unsigned NO_COMPONENT_INDEX = 0xFFFFFFFF;
static bool g_enableThrowError = true;
inline bool isInputEmpty(const Value& inputValue) {
    return inputValue.type == VALUE_TYPE_UNDEFINED && inputValue.int32Value > 0;
}
inline Value getEmptyInputValue() {
    Value emptyInputValue;
    emptyInputValue.int32Value = 1;
    return emptyInputValue;
}
void initGlobalVariables(Assets *assets) {
    if (!g_mainAssetsUncompressed) {
        return;
    }
	auto flowDefinition = static_cast<FlowDefinition *>(assets->flowDefinition);
    auto numVars = flowDefinition->globalVariables.count;
    g_globalVariables = (GlobalVariables *) alloc(
        sizeof(GlobalVariables) +
        (numVars > 0 ? numVars - 1 : 0) * sizeof(Value),
        0xcc34ca8e
    );
    for (uint32_t i = 0; i < numVars; i++) {
		new (g_globalVariables->values + i) Value();
        g_globalVariables->values[i] = flowDefinition->globalVariables[i]->clone();
	}
}
bool isComponentReadyToRun(FlowState *flowState, unsigned componentIndex) {
	auto component = flowState->flow->components[componentIndex];
	if (component->type == defs_v3::COMPONENT_TYPE_CATCH_ERROR_ACTION) {
		return false;
	}
    if (component->type == defs_v3::COMPONENT_TYPE_ON_EVENT_ACTION) {
        return false;
    }
    if (component->type == defs_v3::COMPONENT_TYPE_LABEL_IN_ACTION) {
        return false;
    }
    if (component->type < defs_v3::COMPONENT_TYPE_START_ACTION || component->type >= defs_v3::FIRST_DASHBOARD_WIDGET_COMPONENT_TYPE) {
        return true;
    }
    if (component->type == defs_v3::COMPONENT_TYPE_START_ACTION) {
        if (flowState->parentComponent && flowState->parentComponentIndex != -1) {
            auto flowInputIndex = flowState->parentComponent->inputs[0];
            auto value = flowState->parentFlowState->values[flowInputIndex];
            return value.getType() != VALUE_TYPE_UNDEFINED;
        } else {
            return true;
        }
    }
	int numSeqInputs = 0;
	int numDefinedSeqInputs = 0;
	for (unsigned inputIndex = 0; inputIndex < component->inputs.count; inputIndex++) {
		auto inputValueIndex = component->inputs[inputIndex];
		auto input = flowState->flow->componentInputs[inputValueIndex];
		if (input & COMPONENT_INPUT_FLAG_IS_SEQ_INPUT) {
			numSeqInputs++;
			auto &value = flowState->values[inputValueIndex];
			if (!isInputEmpty(value)) {
				numDefinedSeqInputs++;
			}
		} else {
			if (!(input & COMPONENT_INPUT_FLAG_IS_OPTIONAL)) {
				auto &value = flowState->values[inputValueIndex];
				if (isInputEmpty(value)) {
					return false;
				}
			}
		}
	}
	if (numSeqInputs && !numDefinedSeqInputs) {
		return false;
	}
	return true;
}
static bool pingComponent(FlowState *flowState, unsigned componentIndex, int sourceComponentIndex = -1, int sourceOutputIndex = -1, int targetInputIndex = -1) {
	if (isComponentReadyToRun(flowState, componentIndex)) {
		return addToQueue(flowState, componentIndex, sourceComponentIndex, sourceOutputIndex, targetInputIndex, false);
	}
	return false;
}
static FlowState *initFlowState(Assets *assets, int flowIndex, FlowState *parentFlowState, int parentComponentIndex) {
	auto flowDefinition = static_cast<FlowDefinition *>(assets->flowDefinition);
	auto flow = flowDefinition->flows[flowIndex];
	auto nValues = flow->componentInputs.count + flow->localVariables.count;
	FlowState *flowState = new (
		alloc(
			sizeof(FlowState) +
			nValues * sizeof(Value) +
			flow->components.count * sizeof(ComponenentExecutionState *) +
			flow->components.count * sizeof(bool),
			0x4c3b6ef5
		)
	) FlowState;
	flowState->flowStateIndex = (int)((uint8_t *)flowState - ALLOC_BUFFER);
	flowState->assets = assets;
	flowState->flowDefinition = static_cast<FlowDefinition *>(assets->flowDefinition);
	flowState->flow = flowDefinition->flows[flowIndex];
	flowState->flowIndex = flowIndex;
	flowState->error = false;
	flowState->refCounter = 0;
	flowState->parentFlowState = parentFlowState;
    flowState->executingComponentIndex = NO_COMPONENT_INDEX;
    flowState->timelinePosition = 0;
#if defined(EEZ_FOR_LVGL)
    flowState->lvglWidgetStartIndex = 0;
#endif
    if (parentFlowState) {
        if (parentFlowState->lastChild) {
            parentFlowState->lastChild->nextSibling = flowState;
            flowState->previousSibling = parentFlowState->lastChild;
            parentFlowState->lastChild = flowState;
        } else {
            flowState->previousSibling = nullptr;
            parentFlowState->firstChild = flowState;
            parentFlowState->lastChild = flowState;
        }
		flowState->parentComponentIndex = parentComponentIndex;
		flowState->parentComponent = parentFlowState->flow->components[parentComponentIndex];
	} else {
        if (g_lastFlowState) {
            g_lastFlowState->nextSibling = flowState;
            flowState->previousSibling = g_lastFlowState;
            g_lastFlowState = flowState;
        } else {
            flowState->previousSibling = nullptr;
            g_firstFlowState = flowState;
            g_lastFlowState = flowState;
        }
		flowState->parentComponentIndex = -1;
		flowState->parentComponent = nullptr;
	}
    flowState->firstChild = nullptr;
    flowState->lastChild = nullptr;
    flowState->nextSibling = nullptr;
	flowState->values = (Value *)(flowState + 1);
	flowState->componenentExecutionStates = (ComponenentExecutionState **)(flowState->values + nValues);
    flowState->componenentAsyncStates = (bool *)(flowState->componenentExecutionStates + flow->components.count);
	for (unsigned i = 0; i < nValues; i++) {
		new (flowState->values + i) Value();
	}
	Value emptyInputValue = getEmptyInputValue();
	for (unsigned i = 0; i < flow->componentInputs.count; i++) {
		flowState->values[i] = emptyInputValue;
	}
	for (unsigned i = 0; i < flow->localVariables.count; i++) {
		auto value = flow->localVariables[i];
		flowState->values[flow->componentInputs.count + i] = *value;
	}
	for (unsigned i = 0; i < flow->components.count; i++) {
		flowState->componenentExecutionStates[i] = nullptr;
		flowState->componenentAsyncStates[i] = false;
	}
	onFlowStateCreated(flowState);
	for (unsigned componentIndex = 0; componentIndex < flow->components.count; componentIndex++) {
		pingComponent(flowState, componentIndex);
	}
	return flowState;
}
FlowState *initActionFlowState(int flowIndex, FlowState *parentFlowState, int parentComponentIndex) {
	auto flowState = initFlowState(parentFlowState->assets, flowIndex, parentFlowState, parentComponentIndex);
	if (flowState) {
		flowState->isAction = true;
	}
	return flowState;
}
FlowState *initPageFlowState(Assets *assets, int flowIndex, FlowState *parentFlowState, int parentComponentIndex) {
	auto flowState = initFlowState(assets, flowIndex, parentFlowState, parentComponentIndex);
	if (flowState) {
		flowState->isAction = false;
	}
	return flowState;
}
void incRefCounterForFlowState(FlowState *flowState) {
    flowState->refCounter++;
    for (auto parent = flowState->parentFlowState; parent; parent = parent->parentFlowState) {
        parent->refCounter++;
    }
}
void decRefCounterForFlowState(FlowState *flowState) {
    flowState->refCounter--;
    for (auto parent = flowState->parentFlowState; parent; parent = parent->parentFlowState) {
        parent->refCounter--;
    }
}
bool canFreeFlowState(FlowState *flowState) {
    if (!flowState->isAction) {
        return false;
    }
    if (flowState->refCounter > 0) {
        return false;
    }
    return true;
}
void freeFlowState(FlowState *flowState) {
    auto parentFlowState = flowState->parentFlowState;
    if (parentFlowState) {
        auto componentExecutionState = parentFlowState->componenentExecutionStates[flowState->parentComponentIndex];
        if (componentExecutionState) {
            deallocateComponentExecutionState(parentFlowState, flowState->parentComponentIndex);
            return;
        }
        if (parentFlowState->firstChild == flowState) {
            parentFlowState->firstChild = flowState->nextSibling;
        }
        if (parentFlowState->lastChild == flowState) {
            parentFlowState->lastChild = flowState->previousSibling;
        }
    } else {
        if (g_firstFlowState == flowState) {
            g_firstFlowState = flowState->nextSibling;
        }
        if (g_lastFlowState == flowState) {
            g_lastFlowState = flowState->previousSibling;
        }
    }
    if (flowState->previousSibling) {
        flowState->previousSibling->nextSibling = flowState->nextSibling;
    }
    if (flowState->nextSibling) {
        flowState->nextSibling->previousSibling = flowState->previousSibling;
    }
	auto flow = flowState->flow;
	auto valuesCount = flow->componentInputs.count + flow->localVariables.count;
	for (unsigned int i = 0; i < valuesCount; i++) {
		(flowState->values + i)->~Value();
	}
	for (unsigned i = 0; i < flow->components.count; i++) {
        deallocateComponentExecutionState(flowState, i);
	}
    freeAllChildrenFlowStates(flowState->firstChild);
	onFlowStateDestroyed(flowState);
	flowState->~FlowState();
	free(flowState);
}
void freeAllChildrenFlowStates(FlowState *firstChildFlowState) {
    auto flowState = firstChildFlowState;
    while (flowState != nullptr) {
        auto nextFlowState = flowState->nextSibling;
        freeAllChildrenFlowStates(flowState->firstChild);
        freeFlowState(flowState);
        flowState = nextFlowState;
    }
}
void deallocateComponentExecutionState(FlowState *flowState, unsigned componentIndex) {
    auto executionState = flowState->componenentExecutionStates[componentIndex];
    if (executionState) {
        auto component = flowState->flow->components[componentIndex];
        if (TRACK_REF_COUNTER_FOR_COMPONENT_STATE(component)) {
            decRefCounterForFlowState(flowState);
        }
        flowState->componenentExecutionStates[componentIndex] = nullptr;
        onComponentExecutionStateChanged(flowState, componentIndex);
        ObjectAllocator<ComponenentExecutionState>::deallocate(executionState);
    }
}
void resetSequenceInputs(FlowState *flowState) {
    if (flowState->executingComponentIndex != NO_COMPONENT_INDEX) {
		auto component = flowState->flow->components[flowState->executingComponentIndex];
        flowState->executingComponentIndex = NO_COMPONENT_INDEX;
        if (component->type != defs_v3::COMPONENT_TYPE_OUTPUT_ACTION) {
            for (uint32_t i = 0; i < component->inputs.count; i++) {
                auto inputIndex = component->inputs[i];
                if (flowState->flow->componentInputs[inputIndex] & COMPONENT_INPUT_FLAG_IS_SEQ_INPUT) {
                    auto pValue = &flowState->values[inputIndex];
                    if (!isInputEmpty(*pValue)) {
                        *pValue = getEmptyInputValue();
                        onValueChanged(pValue);
                    }
                }
            }
        }
    }
}
void propagateValue(FlowState *flowState, unsigned componentIndex, unsigned outputIndex, const Value &value) {
    if ((int)componentIndex == -1) {
        auto flowIndex = outputIndex;
        executeCallAction(flowState, -1, flowIndex);
        return;
    }
    resetSequenceInputs(flowState);
	auto component = flowState->flow->components[componentIndex];
	auto componentOutput = component->outputs[outputIndex];
    auto value2 = value.getValue();
	for (unsigned connectionIndex = 0; connectionIndex < componentOutput->connections.count; connectionIndex++) {
		auto connection = componentOutput->connections[connectionIndex];
		auto pValue = &flowState->values[connection->targetInputIndex];
		if (*pValue != value2) {
			*pValue = value2;
				onValueChanged(pValue);
		}
		pingComponent(flowState, connection->targetComponentIndex, componentIndex, outputIndex, connection->targetInputIndex);
	}
}
void propagateValue(FlowState *flowState, unsigned componentIndex, unsigned outputIndex) {
	auto &nullValue = *flowState->flowDefinition->constants[NULL_VALUE_INDEX];
	propagateValue(flowState, componentIndex, outputIndex, nullValue);
}
void propagateValueThroughSeqout(FlowState *flowState, unsigned componentIndex) {
	auto component = flowState->flow->components[componentIndex];
	for (uint32_t i = 0; i < component->outputs.count; i++) {
		if (component->outputs[i]->isSeqOut) {
			propagateValue(flowState, componentIndex, i);
			return;
		}
	}
}
#if EEZ_OPTION_GUI
void getValue(uint16_t dataId, DataOperationEnum operation, const WidgetCursor &widgetCursor, Value &value) {
	if (!isFlowStopped()) {
		FlowState *flowState = widgetCursor.flowState;
		auto flow = flowState->flow;
		WidgetDataItem *widgetDataItem = flow->widgetDataItems[dataId];
		if (widgetDataItem && widgetDataItem->componentIndex != -1 && widgetDataItem->propertyValueIndex != -1) {
			evalProperty(flowState, widgetDataItem->componentIndex, widgetDataItem->propertyValueIndex, value, "doGetFlowValue failed", nullptr, widgetCursor.iterators, operation);
		}
	}
}
void setValue(uint16_t dataId, const WidgetCursor &widgetCursor, const Value& value) {
	if (!isFlowStopped()) {
		FlowState *flowState = widgetCursor.flowState;
		auto flow = flowState->flow;
		WidgetDataItem *widgetDataItem = flow->widgetDataItems[dataId];
		if (widgetDataItem && widgetDataItem->componentIndex != -1 && widgetDataItem->propertyValueIndex != -1) {
			auto component = flow->components[widgetDataItem->componentIndex];
			auto property = component->properties[widgetDataItem->propertyValueIndex];
			Value dstValue;
			if (evalAssignableExpression(flowState, widgetDataItem->componentIndex, property->evalInstructions, dstValue, "doSetFlowValue failed", nullptr, widgetCursor.iterators)) {
				assignValue(flowState, widgetDataItem->componentIndex, dstValue, value);
			}
		}
	}
}
#endif
void assignValue(FlowState *flowState, int componentIndex, Value &dstValue, const Value &srcValue) {
	if (dstValue.getType() == VALUE_TYPE_FLOW_OUTPUT) {
		propagateValue(flowState, componentIndex, dstValue.getUInt16(), srcValue);
	} else if (dstValue.getType() == VALUE_TYPE_NATIVE_VARIABLE) {
#if EEZ_OPTION_GUI
		set(g_widgetCursor, dstValue.getInt(), srcValue);
#else
		setVar(dstValue.getInt(), srcValue);
#endif
	} else {
		Value *pDstValue;
        uint32_t dstValueType = VALUE_TYPE_UNDEFINED;
        if (dstValue.getType() == VALUE_TYPE_ARRAY_ELEMENT_VALUE) {
            auto arrayElementValue = (ArrayElementValue *)dstValue.refValue;
            if (arrayElementValue->arrayValue.isBlob()) {
                auto blobRef = arrayElementValue->arrayValue.getBlob();
                if (arrayElementValue->elementIndex < 0 || arrayElementValue->elementIndex >= (int)blobRef->len) {
                    throwError(flowState, componentIndex, "Can not assign, blob element index out of bounds\n");
                    return;
                }
                int err;
                int32_t elementValue = srcValue.toInt32(&err);
                if (err != 0) {
                    char errorMessage[100];
                    snprintf(errorMessage, sizeof(errorMessage), "Can not non-integer to blob");
                } else if (elementValue < 0 || elementValue > 255) {
                    char errorMessage[100];
                    snprintf(errorMessage, sizeof(errorMessage), "Can not assign %d to blob", (int)elementValue);
                    throwError(flowState, componentIndex, errorMessage);
                } else {
                    blobRef->blob[arrayElementValue->elementIndex] = elementValue;
                }
                return;
            } else {
                auto array = arrayElementValue->arrayValue.getArray();
                if (arrayElementValue->elementIndex < 0 || arrayElementValue->elementIndex >= (int)array->arraySize) {
                    throwError(flowState, componentIndex, "Can not assign, array element index out of bounds\n");
                    return;
                }
                pDstValue = &array->values[arrayElementValue->elementIndex];
            }
        }
#if defined(EEZ_DASHBOARD_API)
        else if (dstValue.getType() == VALUE_TYPE_JSON_MEMBER_VALUE) {
            auto jsonMemberValue = (JsonMemberValue *)dstValue.refValue;
            int err = operationJsonSet(jsonMemberValue->jsonValue.getInt(), jsonMemberValue->propertyName.getString(), &srcValue);
            if (err) {
                throwError(flowState, componentIndex, "Can not assign to JSON member");
            }
            return;
        }
#endif
        else {
            pDstValue = dstValue.pValueValue;
            dstValueType = dstValue.dstValueType;
        }
        if (assignValue(*pDstValue, srcValue, dstValueType)) {
            onValueChanged(pDstValue);
        } else {
            char errorMessage[100];
            snprintf(errorMessage, sizeof(errorMessage), "Can not assign %s to %s\n",
                g_valueTypeNames[pDstValue->type](srcValue), g_valueTypeNames[srcValue.type](*pDstValue)
            );
            throwError(flowState, componentIndex, errorMessage);
        }
	}
}
void clearInputValue(FlowState *flowState, int inputIndex) {
    flowState->values[inputIndex] = Value();
    onValueChanged(flowState->values + inputIndex);
}
void startAsyncExecution(FlowState *flowState, int componentIndex) {
    if (!flowState->componenentAsyncStates[componentIndex]) {
        flowState->componenentAsyncStates[componentIndex] = true;
        onComponentAsyncStateChanged(flowState, componentIndex);
	    incRefCounterForFlowState(flowState);
    }
}
void endAsyncExecution(FlowState *flowState, int componentIndex) {
    if (!g_firstFlowState) {
        return;
    }
    if (flowState->componenentAsyncStates[componentIndex]) {
        flowState->componenentAsyncStates[componentIndex] = false;
        onComponentAsyncStateChanged(flowState, componentIndex);
        decRefCounterForFlowState(flowState);
        do {
            if (!canFreeFlowState(flowState)) {
                break;
            }
            auto temp = flowState->parentFlowState;
            freeFlowState(flowState);
            flowState = temp;
        } while (flowState);
    }
}
void onEvent(FlowState *flowState, FlowEvent flowEvent, Value eventValue) {
	for (unsigned componentIndex = 0; componentIndex < flowState->flow->components.count; componentIndex++) {
		auto component = flowState->flow->components[componentIndex];
		if (component->type == defs_v3::COMPONENT_TYPE_ON_EVENT_ACTION) {
            auto onEventComponent = (OnEventComponent *)component;
            if (onEventComponent->event == flowEvent) {
                flowState->eventValue = eventValue;
                if (!isInQueue(flowState, componentIndex)) {
                    if (!addToQueue(flowState, componentIndex, -1, -1, -1, false)) {
                        return;
                    }
                }
            }
		}
	}
    if (flowEvent == FLOW_EVENT_KEYDOWN) {
        for (auto childFlowState = flowState->firstChild; childFlowState != nullptr; childFlowState = childFlowState->nextSibling) {
            onEvent(childFlowState, flowEvent, eventValue);
        }
    }
}
bool findCatchErrorComponent(FlowState *flowState, FlowState *&catchErrorFlowState, int &catchErrorComponentIndex) {
    if (!flowState) {
        return false;
    }
	for (unsigned componentIndex = 0; componentIndex < flowState->flow->components.count; componentIndex++) {
		auto component = flowState->flow->components[componentIndex];
		if (component->type == defs_v3::COMPONENT_TYPE_CATCH_ERROR_ACTION) {
			catchErrorFlowState = flowState;
			catchErrorComponentIndex = componentIndex;
			return true;
		}
	}
    if (flowState->parentFlowState && flowState->parentComponent && flowState->parentComponent->errorCatchOutput != -1) {
        catchErrorFlowState = flowState->parentFlowState;
        catchErrorComponentIndex = flowState->parentComponentIndex;
        return true;
    }
    return findCatchErrorComponent(flowState->parentFlowState, catchErrorFlowState, catchErrorComponentIndex);
}
void throwError(FlowState *flowState, int componentIndex, const char *errorMessage) {
    auto component = flowState->flow->components[componentIndex];
    if (!g_enableThrowError) {
        return;
    }
#if defined(__EMSCRIPTEN__)
    printf("throwError: %s\n", errorMessage);
#endif
#if defined(EEZ_FOR_LVGL)
    LV_LOG_ERROR("EEZ-FLOW error: %s", errorMessage);
#endif
	if (component->errorCatchOutput != -1) {
		propagateValue(
			flowState,
			componentIndex,
			component->errorCatchOutput,
			Value::makeStringRef(errorMessage, strlen(errorMessage), 0xef6f8414)
		);
	} else {
		FlowState *catchErrorFlowState;
		int catchErrorComponentIndex;
		if (
            findCatchErrorComponent(
                component->type == defs_v3::COMPONENT_TYPE_ERROR_ACTION ? flowState->parentFlowState : flowState,
                catchErrorFlowState,
                catchErrorComponentIndex
            )
        ) {
            for (FlowState *fs = flowState; fs != catchErrorFlowState; fs = fs->parentFlowState) {
                fs->error = true;
            }
            auto component = catchErrorFlowState->flow->components[catchErrorComponentIndex];
            if (component->type == defs_v3::COMPONENT_TYPE_CATCH_ERROR_ACTION) {
                auto catchErrorComponentExecutionState = allocateComponentExecutionState<CatchErrorComponenentExecutionState>(catchErrorFlowState, catchErrorComponentIndex);
                catchErrorComponentExecutionState->message = Value::makeStringRef(errorMessage, strlen(errorMessage), 0x9473eef2);
                if (!addToQueue(catchErrorFlowState, catchErrorComponentIndex, -1, -1, -1, false)) {
                    onFlowError(flowState, componentIndex, errorMessage);
                    stopScriptHook();
                }
            } else {
                propagateValue(
                    catchErrorFlowState,
                    catchErrorComponentIndex,
                    component->errorCatchOutput,
                    Value::makeStringRef(errorMessage, strlen(errorMessage), 0x9473eef3)
                );
            }
		} else {
			onFlowError(flowState, componentIndex, errorMessage);
			stopScriptHook();
		}
	}
}
void throwError(FlowState *flowState, int componentIndex, const char *errorMessage, const char *errorMessageDescription) {
    if (errorMessage) {
        char throwErrorMessage[512];
        snprintf(throwErrorMessage, sizeof(throwErrorMessage), "%s: %s", errorMessage, errorMessageDescription);
        throwError(flowState, componentIndex, throwErrorMessage);
    } else {
        throwError(flowState, componentIndex, errorMessageDescription);
    }
}
void enableThrowError(bool enable) {
    g_enableThrowError = enable;
}
} 
} 
// -----------------------------------------------------------------------------
// flow/watch_list.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeWatchVariableComponent(FlowState *flowState, unsigned componentIndex);
struct WatchListNode {
    FlowState *flowState;
    unsigned componentIndex;
    WatchListNode *prev;
    WatchListNode *next;
};
struct WatchList {
    WatchListNode *first;
    WatchListNode *last;
};
static WatchList g_watchList;
WatchListNode *watchListAdd(FlowState *flowState, unsigned componentIndex) {
    auto node = (WatchListNode *)alloc(sizeof(WatchListNode), 0x00864d67);
    node->prev = g_watchList.last;
    if (g_watchList.last != 0) {
        g_watchList.last->next = node;
    }
    g_watchList.last = node;
    if (g_watchList.first == 0) {
        g_watchList.first = node;
    }
    node->next = 0;
    node->flowState = flowState;
    node->componentIndex = componentIndex;
    incRefCounterForFlowState(flowState);
    return node;
}
void watchListRemove(WatchListNode *node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        g_watchList.first = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        g_watchList.last = node->prev;
    }
    free(node);
}
void visitWatchList() {
    for (auto node = g_watchList.first; node; ) {
        auto nextNode = node->next;
        if (canExecuteStep(node->flowState, node->componentIndex)) {
            executeWatchVariableComponent(node->flowState, node->componentIndex);
        }
        decRefCounterForFlowState(node->flowState);
        if (canFreeFlowState(node->flowState)) {
            freeFlowState(node->flowState);
            watchListRemove(node);
        } else {
            incRefCounterForFlowState(node->flowState);
        }
        node = nextNode;
    }
}
void watchListReset() {
    for (auto node = g_watchList.first; node;) {
        auto nextNode = node->next;
        watchListRemove(node);
        node = nextNode;
    }
}
} 
} 
// -----------------------------------------------------------------------------
// flow/queue.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
#if !defined(EEZ_FLOW_QUEUE_SIZE)
#define EEZ_FLOW_QUEUE_SIZE 1000
#endif
static const unsigned QUEUE_SIZE = EEZ_FLOW_QUEUE_SIZE;
static struct {
	FlowState *flowState;
	unsigned componentIndex;
    bool continuousTask;
} g_queue[QUEUE_SIZE];
static unsigned g_queueHead;
static unsigned g_queueTail;
static unsigned g_queueMax;
static bool g_queueIsFull = false;
unsigned g_numContinuousTaskInQueue;
void queueReset() {
	g_queueHead = 0;
	g_queueTail = 0;
	g_queueMax  = 0;
	g_queueIsFull = false;
    g_numContinuousTaskInQueue = 0;
}
size_t getQueueSize() {
	if (g_queueHead == g_queueTail) {
		if (g_queueIsFull) {
			return QUEUE_SIZE;
		}
		return 0;
	}
	if (g_queueHead < g_queueTail) {
		return g_queueTail - g_queueHead;
	}
	return QUEUE_SIZE - g_queueHead + g_queueTail;
}
size_t getMaxQueueSize() {
	return g_queueMax;
}
bool addToQueue(FlowState *flowState, unsigned componentIndex, int sourceComponentIndex, int sourceOutputIndex, int targetInputIndex, bool continuousTask) {
	if (g_queueIsFull) {
        throwError(flowState, componentIndex, "Execution queue is full\n");
		return false;
	}
	g_queue[g_queueTail].flowState = flowState;
	g_queue[g_queueTail].componentIndex = componentIndex;
    g_queue[g_queueTail].continuousTask = continuousTask;
	g_queueTail = (g_queueTail + 1) % QUEUE_SIZE;
	if (g_queueHead == g_queueTail) {
		g_queueIsFull = true;
	}
	size_t queueSize = getQueueSize();
	g_queueMax = g_queueMax < queueSize ? queueSize : g_queueMax;
    if (!continuousTask) {
        ++g_numContinuousTaskInQueue;
	    onAddToQueue(flowState, sourceComponentIndex, sourceOutputIndex, componentIndex, targetInputIndex);
    }
    incRefCounterForFlowState(flowState);
	return true;
}
bool peekNextTaskFromQueue(FlowState *&flowState, unsigned &componentIndex, bool &continuousTask) {
	if (g_queueHead == g_queueTail && !g_queueIsFull) {
		return false;
	}
	flowState = g_queue[g_queueHead].flowState;
	componentIndex = g_queue[g_queueHead].componentIndex;
    continuousTask = g_queue[g_queueHead].continuousTask;
	return true;
}
void removeNextTaskFromQueue() {
	auto flowState = g_queue[g_queueHead].flowState;
    decRefCounterForFlowState(flowState);
    auto continuousTask = g_queue[g_queueHead].continuousTask;
	g_queueHead = (g_queueHead + 1) % QUEUE_SIZE;
	g_queueIsFull = false;
    if (!continuousTask) {
        --g_numContinuousTaskInQueue;
	    onRemoveFromQueue();
    }
}
bool isInQueue(FlowState *flowState, unsigned componentIndex) {
	if (g_queueHead == g_queueTail && !g_queueIsFull) {
		return false;
	}
    unsigned int it = g_queueHead;
    while (true) {
		if (g_queue[it].flowState == flowState && g_queue[it].componentIndex == componentIndex) {
            return true;
		}
        it = (it + 1) % QUEUE_SIZE;
        if (it == g_queueTail) {
            break;
        }
	}
    return false;
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/animate.cpp
// -----------------------------------------------------------------------------
#if EEZ_OPTION_GUI
using namespace eez::gui;
#endif
namespace eez {
namespace flow {
struct AnimateComponenentExecutionState : public ComponenentExecutionState {
    float startPosition;
    float endPosition;
    float speed;
    uint32_t startTimestamp;
};
void executeAnimateComponent(FlowState *flowState, unsigned componentIndex) {
	auto state = (AnimateComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
	if (!state) {
        Value fromValue;
        if (!evalProperty(flowState, componentIndex, defs_v3::ANIMATE_ACTION_COMPONENT_PROPERTY_FROM, fromValue, "Failed to evaluate From in Animate")) {
            return;
        }
        Value toValue;
        if (!evalProperty(flowState, componentIndex, defs_v3::ANIMATE_ACTION_COMPONENT_PROPERTY_TO, toValue, "Failed to evaluate To in Animate")) {
            return;
        }
        Value speedValue;
        if (!evalProperty(flowState, componentIndex, defs_v3::ANIMATE_ACTION_COMPONENT_PROPERTY_SPEED, speedValue, "Failed to evaluate Speed in Animate")) {
            return;
        }
        float from = fromValue.toFloat();
        float to = toValue.toFloat();
        float speed = speedValue.toFloat();
        if (speed == 0) {
            flowState->timelinePosition = to;
            onFlowStateTimelineChanged(flowState);
            propagateValueThroughSeqout(flowState, componentIndex);
        } else {
		    state = allocateComponentExecutionState<AnimateComponenentExecutionState>(flowState, componentIndex);
            state->startPosition = from;
            state->endPosition = to;
            state->speed = speed;
            state->startTimestamp = millis();
            if (!addToQueue(flowState, componentIndex, -1, -1, -1, true)) {
                return;
            }
        }
    } else {
        float currentTime;
        if (state->startPosition < state->endPosition) {
            currentTime = state->startPosition + state->speed * (millis() - state->startTimestamp) / 1000.0f;
            if (currentTime >= state->endPosition) {
                currentTime = state->endPosition;
            }
        } else {
            currentTime = state->startPosition - state->speed * (millis() - state->startTimestamp) / 1000.0f;
            if (currentTime <= state->endPosition) {
                currentTime = state->endPosition;
            }
        }
        flowState->timelinePosition = currentTime;
        onFlowStateTimelineChanged(flowState);
        if (currentTime == state->endPosition) {
            deallocateComponentExecutionState(flowState, componentIndex);
            propagateValueThroughSeqout(flowState, componentIndex);
        } else {
            if (!addToQueue(flowState, componentIndex, -1, -1, -1, true)) {
                return;
            }
        }
    }
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/call_action.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
namespace eez {
namespace flow {
void executeCallAction(FlowState *flowState, unsigned componentIndex, int flowIndex) {
	if (flowIndex >= (int)flowState->flowDefinition->flows.count) {
		executeActionFunction(flowIndex - flowState->flowDefinition->flows.count);
		propagateValueThroughSeqout(flowState, componentIndex);
		return;
	}
	FlowState *actionFlowState = initActionFlowState(flowIndex, flowState, componentIndex);
	if (canFreeFlowState(actionFlowState)) {
        freeFlowState(actionFlowState);
        if ((int)componentIndex != -1) {
		    propagateValueThroughSeqout(flowState, componentIndex);
        }
	}
}
void executeCallActionComponent(FlowState *flowState, unsigned componentIndex) {
	auto component = (CallActionActionComponent *)flowState->flow->components[componentIndex];
	auto flowIndex = component->flowIndex;
	if (flowIndex < 0) {
		throwError(flowState, componentIndex, "Invalid action flow index in CallAction\n");
		return;
	}
    executeCallAction(flowState, componentIndex, flowIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/compare.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
struct CompareActionComponent : public Component {
	uint8_t conditionInstructions[1];
};
void executeCompareComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (CompareActionComponent *)flowState->flow->components[componentIndex];
    Value conditionValue;
    if (!evalExpression(flowState, componentIndex, component->conditionInstructions, conditionValue, "Failed to evaluate Condition in Compare")) {
        return;
    }
    int err;
    bool result = conditionValue.toBool(&err);
    if (err == 0) {
        if (result) {
            propagateValue(flowState, componentIndex, 1, Value(true, VALUE_TYPE_BOOLEAN));
        } else {
            propagateValue(flowState, componentIndex, 2, Value(false, VALUE_TYPE_BOOLEAN));
        }
    } else {
        throwError(flowState, componentIndex, "Failed to convert Value to boolean in IsTrue\n");
        return;
    }
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/catch_error.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeCatchErrorComponent(FlowState *flowState, unsigned componentIndex) {
	auto catchErrorComponentExecutionState = (CatchErrorComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
	propagateValue(flowState, componentIndex, 1, catchErrorComponentExecutionState->message);
    deallocateComponentExecutionState(flowState, componentIndex);
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/constant.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
struct ConstantActionComponent : public Component {
	uint16_t valueIndex;
};
void executeConstantComponent(FlowState *flowState, unsigned componentIndex) {
	auto component = (ConstantActionComponent *)flowState->flow->components[componentIndex];
	auto &sourceValue = *flowState->flowDefinition->constants[component->valueIndex];
	propagateValue(flowState, componentIndex, 1, sourceValue);
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/counter.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
struct CounterComponenentExecutionState : public ComponenentExecutionState {
    int counter;
};
void executeCounterComponent(FlowState *flowState, unsigned componentIndex) {
    auto counterComponenentExecutionState = (CounterComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
    if (!counterComponenentExecutionState) {
        Value counterValue;
        if (!evalProperty(flowState, componentIndex, defs_v3::COUNTER_ACTION_COMPONENT_PROPERTY_COUNT_VALUE, counterValue, "Failed to evaluate countValue in Counter")) {
            return;
        }
        counterComponenentExecutionState = allocateComponentExecutionState<CounterComponenentExecutionState>(flowState, componentIndex);
        counterComponenentExecutionState->counter = counterValue.getInt();
    }
    if (counterComponenentExecutionState->counter > 0) {
        counterComponenentExecutionState->counter--;
        propagateValueThroughSeqout(flowState, componentIndex);
    } else {
        deallocateComponentExecutionState(flowState, componentIndex);
        propagateValue(flowState, componentIndex, 1);
    }
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/delay.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
struct DelayComponenentExecutionState : public ComponenentExecutionState {
	uint32_t waitUntil;
};
void executeDelayComponent(FlowState *flowState, unsigned componentIndex) {
	auto delayComponentExecutionState = (DelayComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
	if (!delayComponentExecutionState) {
		Value value;
		if (!evalProperty(flowState, componentIndex, defs_v3::DELAY_ACTION_COMPONENT_PROPERTY_MILLISECONDS, value, "Failed to evaluate Milliseconds in Delay")) {
			return;
		}
		double milliseconds = value.toDouble();
		if (!isNaN(milliseconds)) {
			delayComponentExecutionState = allocateComponentExecutionState<DelayComponenentExecutionState>(flowState, componentIndex);
			delayComponentExecutionState->waitUntil = millis() + (uint32_t)floor(milliseconds);
		} else {
			throwError(flowState, componentIndex, "Invalid Milliseconds value in Delay\n");
			return;
		}
		if (!addToQueue(flowState, componentIndex, -1, -1, -1, true)) {
			return;
		}
	} else {
		if (millis() >= delayComponentExecutionState->waitUntil) {
			deallocateComponentExecutionState(flowState, componentIndex);
			propagateValueThroughSeqout(flowState, componentIndex);
		} else {
			if (!addToQueue(flowState, componentIndex, -1, -1, -1, true)) {
				return;
			}
		}
	}
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/end.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeEndComponent(FlowState *flowState, unsigned componentIndex) {
	if (flowState->parentFlowState && flowState->isAction) {
        if (flowState->parentComponentIndex != -1) {
		    propagateValueThroughSeqout(flowState->parentFlowState, flowState->parentComponentIndex);
        }
	} else {
		stopScriptHook();
	}
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/error.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeErrorComponent(FlowState *flowState, unsigned componentIndex) {
	Value expressionValue;
	if (!evalProperty(flowState, componentIndex, defs_v3::EVAL_EXPR_ACTION_COMPONENT_PROPERTY_EXPRESSION, expressionValue, "Failed to evaluate Message in Error")) {
		return;
	}
	throwError(flowState, componentIndex, expressionValue.getString());
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/input.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
namespace eez {
namespace flow {
bool getCallActionValue(FlowState *flowState, unsigned componentIndex, Value &value) {
	auto component = flowState->flow->components[componentIndex];
	if (!flowState->parentFlowState) {
		throwError(flowState, componentIndex, "No parentFlowState in Input\n");
		return false;
	}
	if (!flowState->parentComponent) {
		throwError(flowState, componentIndex, "No parentComponent in Input\n");
		return false;
	}
    auto callActionComponent = (CallActionActionComponent *)flowState->parentComponent;
    uint8_t callActionComponentInputIndex = callActionComponent->inputsStartIndex;
    if (component->type == defs_v3::COMPONENT_TYPE_INPUT_ACTION) {
        auto inputActionComponent = (InputActionComponent *)component;
        callActionComponentInputIndex += inputActionComponent->inputIndex;
    } else {
        callActionComponentInputIndex -= 1;
    }
    if (callActionComponentInputIndex >= callActionComponent->inputs.count) {
        throwError(flowState, componentIndex, "Invalid input index in Input\n");
        return false;
    }
    auto &parentComponentInputs = callActionComponent->inputs;
    auto parentFlowInputIndex = parentComponentInputs[callActionComponentInputIndex];
    auto parentFlow = flowState->flowDefinition->flows[flowState->parentFlowState->flowIndex];
    if (parentFlowInputIndex >= parentFlow->componentInputs.count) {
        throwError(flowState, componentIndex, "Invalid input index of parent component in Input\n");
        return false;
    }
    value = flowState->parentFlowState->values[parentFlowInputIndex];
    return true;
}
void executeInputComponent(FlowState *flowState, unsigned componentIndex) {
	Value value;
    if (getCallActionValue(flowState, componentIndex, value)) {
        auto inputActionComponentExecutionState = (InputActionComponentExecutionState *)flowState->componenentExecutionStates[componentIndex];
        if (!inputActionComponentExecutionState) {
            inputActionComponentExecutionState = allocateComponentExecutionState<InputActionComponentExecutionState>(flowState, componentIndex);
        }
        propagateValue(flowState, componentIndex, 0, value);
        inputActionComponentExecutionState->value = value;
    }
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/expr_eval.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeEvalExprComponent(FlowState *flowState, unsigned componentIndex) {
	Value expressionValue;
	if (!evalProperty(flowState, componentIndex, defs_v3::EVAL_EXPR_ACTION_COMPONENT_PROPERTY_EXPRESSION, expressionValue, "Failed to evaluate Expression in EvalExpr")) {
		return;
	}
	propagateValue(flowState, componentIndex, 1, expressionValue);
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/is_true.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeIsTrueComponent(FlowState *flowState, unsigned componentIndex) {
	Value srcValue;
	if (!evalProperty(flowState, componentIndex, defs_v3::IS_TRUE_ACTION_COMPONENT_PROPERTY_VALUE, srcValue, "Failed to evaluate Value in IsTrue")) {
		return;
	}
    int err;
    bool result = srcValue.toBool(&err);
    if (err == 0) {
        if (result) {
            propagateValue(flowState, componentIndex, 1, Value(true, VALUE_TYPE_BOOLEAN));
        } else {
            propagateValue(flowState, componentIndex, 2, Value(false, VALUE_TYPE_BOOLEAN));
        }
    } else {
        throwError(flowState, componentIndex, "Failed to convert Value to boolean in IsTrue\n");
        return;
    }
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/label_in.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeLabelInComponent(FlowState *flowState, unsigned componentIndex) {
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/line_chart_widget.cpp
// -----------------------------------------------------------------------------
#if EEZ_OPTION_GUI
namespace eez {
namespace flow {
LineChartWidgetComponenentExecutionState::LineChartWidgetComponenentExecutionState()
    : data(nullptr)
{
}
LineChartWidgetComponenentExecutionState::~LineChartWidgetComponenentExecutionState() {
    if (data != nullptr) {
        auto xValues = (Value *)data;
        for (uint32_t i = 0; i < maxPoints; i++) {
            (xValues + i)->~Value();
        }
        eez::free(data);
    }
    for (uint32_t i = 0; i < maxPoints; i++) {
		(lineLabels + i)->~Value();
	}
    eez::free(lineLabels);
}
void LineChartWidgetComponenentExecutionState::init(uint32_t numLines_, uint32_t maxPoints_) {
    numLines = numLines_;
    maxPoints = maxPoints_;
    data = eez::alloc(maxPoints * sizeof(Value) + maxPoints * numLines * sizeof(float), 0xe4945fea);
    auto xValues = (Value *)data;
    for (uint32_t i = 0; i < maxPoints; i++) {
		new (xValues + i) Value();
	}
    numPoints = 0;
    startPointIndex = 0;
    lineLabels = (Value *)eez::alloc(numLines * sizeof(Value *), 0xe8afd215);
    for (uint32_t i = 0; i < numLines; i++) {
		new (lineLabels + i) Value();
	}
    updated = true;
}
Value LineChartWidgetComponenentExecutionState::getX(int pointIndex) {
    auto xValues = (Value *)data;
    return xValues[pointIndex];
}
void LineChartWidgetComponenentExecutionState::setX(int pointIndex, Value& value) {
    auto xValues = (Value *)data;
    xValues[pointIndex] = value;
}
float LineChartWidgetComponenentExecutionState::getY(int pointIndex, int lineIndex) {
    auto yValues = (float *)((Value *)data + maxPoints);
    return *(yValues + pointIndex * numLines + lineIndex);
}
void LineChartWidgetComponenentExecutionState::setY(int pointIndex, int lineIndex, float value) {
    auto yValues = (float *)((Value *)data + maxPoints);
    *(yValues + pointIndex * numLines + lineIndex) = value;
}
bool LineChartWidgetComponenentExecutionState::onInputValue(FlowState *flowState, unsigned componentIndex) {
    auto component = (LineChartWidgetComponenent *)flowState->flow->components[componentIndex];
    uint32_t pointIndex;
    if (numPoints < component->maxPoints) {
        pointIndex = numPoints++;
    } else {
        startPointIndex = (startPointIndex + 1) % component->maxPoints;
        pointIndex = (startPointIndex + component->maxPoints - 1) % component->maxPoints;
    }
    Value value;
    if (!evalExpression(flowState, componentIndex, component->xValue, value, "Failed to evaluate x value in LineChartWidget")) {
        return false;
    }
    int err;
    value.toDouble(&err);
    if (err) {
        throwError(flowState, componentIndex, "X value not an number or date");
        return false;
    }
    setX(pointIndex, value);
    for (uint32_t lineIndex = 0; lineIndex < numLines; lineIndex++) {
        char errorMessage[256];
        snprintf(errorMessage, sizeof(errorMessage), "Failed to evaluate line value no. %d in LineChartWidget", (int)(lineIndex + 1));
        Value value;
        if (!evalExpression(flowState, componentIndex, component->lines[lineIndex]->value, value, errorMessage)) {
            return false;
        }
        int err;
        auto y = value.toFloat(&err);
        if (err) {
            snprintf(errorMessage, sizeof(errorMessage), "Can't convert line value no. %d to float", (int)(lineIndex + 1));
            throwError(flowState, componentIndex, errorMessage);
            return false;
        }
        setY(pointIndex, lineIndex, y);
    }
    return true;
}
void executeLineChartWidgetComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (LineChartWidgetComponenent *)flowState->flow->components[componentIndex];
    auto executionState = (LineChartWidgetComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
    if (!executionState) {
        executionState = allocateComponentExecutionState<LineChartWidgetComponenentExecutionState>(flowState, componentIndex);
        executionState->init(component->lines.count, component->maxPoints);
        for (uint32_t lineIndex = 0; lineIndex < component->lines.count; lineIndex++) {
            char errorMessage[256];
            snprintf(errorMessage, sizeof(errorMessage), "Failed to evaluate line label no. %d in LineChartWidget", (int)(lineIndex + 1));
            if (!evalExpression(flowState, componentIndex, component->lines[lineIndex]->label, executionState->lineLabels[lineIndex], errorMessage)) {
                return;
            }
        }
    }
    int resetInputIndex = 0;
    int valueInputIndex = 1;
    if (flowState->values[component->inputs[resetInputIndex]].type != VALUE_TYPE_UNDEFINED) {
        executionState->numPoints = 0;
        executionState->startPointIndex = 0;
        executionState->updated = true;
        clearInputValue(flowState, component->inputs[resetInputIndex]);
    }
    auto valueInputIndexInFlow = component->inputs[valueInputIndex];
    auto inputValue = flowState->values[valueInputIndexInFlow];
    if (inputValue.type != VALUE_TYPE_UNDEFINED) {
        if (inputValue.isArray() && inputValue.getArray()->arrayType == defs_v3::ARRAY_TYPE_ANY) {
            auto array = inputValue.getArray();
            bool updated = false;
            executionState->startPointIndex = 0;
            executionState->numPoints = 0;
            for (uint32_t elementIndex = 0; elementIndex < array->arraySize; elementIndex++) {
                flowState->values[valueInputIndexInFlow] = array->values[elementIndex];
                if (executionState->onInputValue(flowState, componentIndex)) {
                    updated = true;
                } else {
                    break;
                }
            }
            if (updated) {
                executionState->updated = true;
            }
        } else {
            if (executionState->onInputValue(flowState, componentIndex)) {
                executionState->updated = true;
            }
        }
        clearInputValue(flowState, valueInputIndexInFlow);
    }
}
} 
} 
#endif 
// -----------------------------------------------------------------------------
// flow/components/label_out.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
struct LabelOutActionComponent : public Component {
    uint16_t labelInComponentIndex;
};
void executeLabelOutComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (LabelOutActionComponent *)flowState->flow->components[componentIndex];
    if ((int)component->labelInComponentIndex != -1) {
        propagateValueThroughSeqout(flowState, component->labelInComponentIndex);
    }
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/log.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeLogComponent(FlowState *flowState, unsigned componentIndex) {
    Value value;
    if (!evalProperty(flowState, componentIndex, defs_v3::LOG_ACTION_COMPONENT_PROPERTY_VALUE, value, "Failed to evaluate Message in Log")) {
        return;
    }
    Value strValue = value.toString(0x0f9812ee);
    const char *valueStr = strValue.getString();
    if (valueStr && *valueStr) {
      logInfo(flowState, componentIndex, valueStr);
    }
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/loop.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
struct LoopComponenentExecutionState : public ComponenentExecutionState {
    Value dstValue;
    Value toValue;
    Value currentValue;
};
void executeLoopComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = flowState->flow->components[componentIndex];
    auto loopComponentExecutionState = (LoopComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
    static const unsigned START_INPUT_INDEX = 0;
    auto startInputIndex = component->inputs[START_INPUT_INDEX];
    if (flowState->values[startInputIndex].type != VALUE_TYPE_UNDEFINED) {
        if (loopComponentExecutionState) {
            deallocateComponentExecutionState(flowState, componentIndex);
            loopComponentExecutionState = nullptr;
        }
    } else {
        if (!loopComponentExecutionState) {
            return;
        }
    }
    Value stepValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::LOOP_ACTION_COMPONENT_PROPERTY_STEP, stepValue, "Failed to evaluate Step in Loop")) {
        return;
    }
    Value currentValue;
    if (!loopComponentExecutionState) {
        Value dstValue;
        if (!evalAssignableProperty(flowState, componentIndex, defs_v3::LOOP_ACTION_COMPONENT_PROPERTY_VARIABLE, dstValue, "Failed to evaluate Variable in Loop")) {
            return;
        }
        Value fromValue;
        if (!evalProperty(flowState, componentIndex, defs_v3::LOOP_ACTION_COMPONENT_PROPERTY_FROM, fromValue, "Failed to evaluate From in Loop")) {
            return;
        }
        Value toValue;
        if (!evalProperty(flowState, componentIndex, defs_v3::LOOP_ACTION_COMPONENT_PROPERTY_TO, toValue, "Failed to evaluate To in Loop")) {
            return;
        }
        loopComponentExecutionState = allocateComponentExecutionState<LoopComponenentExecutionState>(flowState, componentIndex);
        loopComponentExecutionState->dstValue = dstValue;
        loopComponentExecutionState->toValue = toValue;
		currentValue = fromValue;
    } else {
        if (loopComponentExecutionState->dstValue.getType() == VALUE_TYPE_FLOW_OUTPUT) {
            currentValue = op_add(loopComponentExecutionState->currentValue, stepValue);
        } else {
            currentValue = op_add(loopComponentExecutionState->dstValue, stepValue);
        }
    }
    if (loopComponentExecutionState->dstValue.getType() == VALUE_TYPE_FLOW_OUTPUT) {
        loopComponentExecutionState->currentValue = currentValue;
    } else {
        assignValue(flowState, componentIndex, loopComponentExecutionState->dstValue, currentValue);
    }
    bool condition;
    if (stepValue.toDouble(nullptr) > 0) {
        condition = op_great(currentValue, loopComponentExecutionState->toValue).toBool();
    } else {
        condition = op_less(currentValue, loopComponentExecutionState->toValue).toBool();
    }
    if (condition) {
        deallocateComponentExecutionState(flowState, componentIndex);
        propagateValue(flowState, componentIndex, 1);
    } else {
        if (loopComponentExecutionState->dstValue.getType() == VALUE_TYPE_FLOW_OUTPUT) {
            assignValue(flowState, componentIndex, loopComponentExecutionState->dstValue, currentValue);
        }
        propagateValueThroughSeqout(flowState, componentIndex);
    }
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/lvgl.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
#if defined(EEZ_FOR_LVGL)
namespace eez {
namespace flow {
void anim_callback_set_x(lv_anim_t * a, int32_t v) { lv_obj_set_x((lv_obj_t *)a->user_data, v); }
int32_t anim_callback_get_x(lv_anim_t * a) { return lv_obj_get_x_aligned((lv_obj_t *)a->user_data); }
void anim_callback_set_y(lv_anim_t * a, int32_t v) { lv_obj_set_y((lv_obj_t *)a->user_data, v); }
int32_t anim_callback_get_y(lv_anim_t * a) { return lv_obj_get_y_aligned((lv_obj_t *)a->user_data); }
void anim_callback_set_width(lv_anim_t * a, int32_t v) { lv_obj_set_width((lv_obj_t *)a->user_data, v); }
int32_t anim_callback_get_width(lv_anim_t * a) { return lv_obj_get_width((lv_obj_t *)a->user_data); }
void anim_callback_set_height(lv_anim_t * a, int32_t v) { lv_obj_set_height((lv_obj_t *)a->user_data, v); }
int32_t anim_callback_get_height(lv_anim_t * a) { return lv_obj_get_height((lv_obj_t *)a->user_data); }
void anim_callback_set_opacity(lv_anim_t * a, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)a->user_data, v, 0); }
int32_t anim_callback_get_opacity(lv_anim_t * a) { return lv_obj_get_style_opa((lv_obj_t *)a->user_data, 0); }
void anim_callback_set_image_zoom(lv_anim_t * a, int32_t v) { lv_img_set_zoom((lv_obj_t *)a->user_data, v); }
int32_t anim_callback_get_image_zoom(lv_anim_t * a) { return lv_img_get_zoom((lv_obj_t *)a->user_data); }
void anim_callback_set_image_angle(lv_anim_t * a, int32_t v) { lv_img_set_angle((lv_obj_t *)a->user_data, v); }
int32_t anim_callback_get_image_angle(lv_anim_t * a) { return lv_img_get_angle((lv_obj_t *)a->user_data); }
void (*anim_set_callbacks[])(lv_anim_t *a, int32_t v) = {
    anim_callback_set_x,
    anim_callback_set_y,
    anim_callback_set_width,
    anim_callback_set_height,
    anim_callback_set_opacity,
    anim_callback_set_image_zoom,
    anim_callback_set_image_angle
};
int32_t (*anim_get_callbacks[])(lv_anim_t *a) = {
    anim_callback_get_x,
    anim_callback_get_y,
    anim_callback_get_width,
    anim_callback_get_height,
    anim_callback_get_opacity,
    anim_callback_get_image_zoom,
    anim_callback_get_image_angle
};
int32_t (*anim_path_callbacks[])(const lv_anim_t *a) = {
    lv_anim_path_linear,
    lv_anim_path_ease_in,
    lv_anim_path_ease_out,
    lv_anim_path_ease_in_out,
    lv_anim_path_overshoot,
    lv_anim_path_bounce
};
enum PropertyCode {
    NONE,
    ARC_VALUE,
    BAR_VALUE,
    BASIC_X,
    BASIC_Y,
    BASIC_WIDTH,
    BASIC_HEIGHT,
    BASIC_OPACITY,
    BASIC_HIDDEN,
    BASIC_CHECKED,
    BASIC_DISABLED,
    DROPDOWN_SELECTED,
    IMAGE_IMAGE,
    IMAGE_ANGLE,
    IMAGE_ZOOM,
    LABEL_TEXT,
    ROLLER_SELECTED,
    SLIDER_VALUE,
    KEYBOARD_TEXTAREA
};
struct LVGLExecutionState : public ComponenentExecutionState {
    uint32_t actionIndex;
};
void executeLVGLComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (LVGLComponent *)flowState->flow->components[componentIndex];
    char errorMessage[256];
    auto executionState = (LVGLExecutionState *)flowState->componenentExecutionStates[componentIndex];
    for (uint32_t actionIndex = executionState ? executionState->actionIndex : 0; actionIndex < component->actions.count; actionIndex++) {
        auto general = (LVGLComponent_ActionType *)component->actions[actionIndex];
        if (general->action == CHANGE_SCREEN) {
            auto specific = (LVGLComponent_ChangeScreen_ActionType *)general;
            if (specific->screen == -1) {
                eez_flow_pop_screen((lv_scr_load_anim_t)specific->fadeMode, specific->speed, specific->delay);
            } else {
                eez_flow_push_screen(specific->screen, (lv_scr_load_anim_t)specific->fadeMode, specific->speed, specific->delay);
            }
        } else if (general->action == PLAY_ANIMATION) {
            auto specific = (LVGLComponent_PlayAnimation_ActionType *)general;
            auto target = getLvglObjectFromIndexHook(flowState->lvglWidgetStartIndex + specific->target);
            if (!target) {
                if (!executionState) {
                    executionState = allocateComponentExecutionState<LVGLExecutionState>(flowState, componentIndex);
                }
                executionState->actionIndex = actionIndex;
                addToQueue(flowState, componentIndex, -1, -1, -1, true);
                return;
            }
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_time(&anim, specific->time);
            lv_anim_set_user_data(&anim, target);
            lv_anim_set_custom_exec_cb(&anim, anim_set_callbacks[specific->property]);
            lv_anim_set_values(&anim, specific->start, specific->end);
            lv_anim_set_path_cb(&anim, anim_path_callbacks[specific->path]);
            lv_anim_set_delay(&anim, specific->delay);
            lv_anim_set_early_apply(&anim, specific->flags & ANIMATION_ITEM_FLAG_INSTANT ? true : false);
            if (specific->flags & ANIMATION_ITEM_FLAG_RELATIVE) {
                lv_anim_set_get_value_cb(&anim, anim_get_callbacks[specific->property]);
            }
            lv_anim_start(&anim);
        } else if (general->action == SET_PROPERTY) {
            auto specific = (LVGLComponent_SetProperty_ActionType *)general;
            auto target = getLvglObjectFromIndexHook(flowState->lvglWidgetStartIndex + specific->target);
            if (!target) {
                if (!executionState) {
                    executionState = allocateComponentExecutionState<LVGLExecutionState>(flowState, componentIndex);
                }
                executionState->actionIndex = actionIndex;
                addToQueue(flowState, componentIndex, -1, -1, -1, true);
                return;
            }
            if (specific->property == KEYBOARD_TEXTAREA) {
                auto textarea = specific->textarea != -1 ? getLvglObjectFromIndexHook(flowState->lvglWidgetStartIndex + specific->textarea) : nullptr;
                if (!textarea) {
                    if (!executionState) {
                        executionState = allocateComponentExecutionState<LVGLExecutionState>(flowState, componentIndex);
                    }
                    executionState->actionIndex = actionIndex;
                    addToQueue(flowState, componentIndex, -1, -1, -1, true);
                    return;
                }
                lv_keyboard_set_textarea(target, textarea);
            } else {
                Value value;
                snprintf(errorMessage, sizeof(errorMessage), "Failed to evaluate Value in LVGL Set Property action #%d", (int)(actionIndex + 1));
                if (!evalExpression(flowState, componentIndex, specific->value, value, errorMessage)) {
                    return;
                }
                if (specific->property == IMAGE_IMAGE || specific->property == LABEL_TEXT) {
                    const char *strValue = value.toString(0xe42b3ca2).getString();
                    if (specific->property == IMAGE_IMAGE) {
                        const void *src = getLvglImageByNameHook(strValue);
                        if (src) {
                            lv_img_set_src(target, src);
                        } else {
                            snprintf(errorMessage, sizeof(errorMessage), "Image \"%s\" not found in LVGL Set Property action #%d", strValue, (int)(actionIndex + 1));
                            throwError(flowState, componentIndex, errorMessage);
                        }
                    } else {
                        lv_label_set_text(target, strValue ? strValue : "");
                    }
                } else if (specific->property == BASIC_HIDDEN) {
                    int err;
                    bool booleanValue = value.toBool(&err);
                    if (err) {
                        snprintf(errorMessage, sizeof(errorMessage), "Failed to convert value to boolean in LVGL Set Property action #%d", (int)(actionIndex + 1));
                        throwError(flowState, componentIndex, errorMessage);
                        return;
                    }
                    lv_state_t flag = LV_OBJ_FLAG_HIDDEN;
                    if (booleanValue) lv_obj_add_flag(target, flag);
                    else lv_obj_clear_flag(target, flag);
                } else if (specific->property == BASIC_CHECKED || specific->property == BASIC_DISABLED) {
                    int err;
                    bool booleanValue = value.toBool(&err);
                    if (err) {
                        snprintf(errorMessage, sizeof(errorMessage), "Failed to convert value to boolean in LVGL Set Property action #%d", (int)(actionIndex + 1));
                        throwError(flowState, componentIndex, errorMessage);
                        return;
                    }
                    lv_state_t state = specific->property == BASIC_CHECKED ? LV_STATE_CHECKED : LV_STATE_DISABLED;
                    if (booleanValue) lv_obj_add_state(target, state);
                    else lv_obj_clear_state(target, state);
                } else {
                    int err;
                    int32_t intValue = value.toInt32(&err);
                    if (err) {
                        snprintf(errorMessage, sizeof(errorMessage), "Failed to convert value to integer in LVGL Set Property action #%d", (int)(actionIndex + 1));
                        throwError(flowState, componentIndex, errorMessage);
                        return;
                    }
                    if (specific->property == ARC_VALUE) {
                        lv_arc_set_value(target, intValue);
                    } else if (specific->property == BAR_VALUE) {
                        lv_bar_set_value(target, intValue, specific->animated ? LV_ANIM_ON : LV_ANIM_OFF);
                    } else if (specific->property == BASIC_X) {
                        lv_obj_set_x(target, intValue);
                    } else if (specific->property == BASIC_Y) {
                        lv_obj_set_y(target, intValue);
                    } else if (specific->property == BASIC_WIDTH) {
                        lv_obj_set_width(target, intValue);
                    } else if (specific->property == BASIC_HEIGHT) {
                        lv_obj_set_height(target, intValue);
                    } else if (specific->property == BASIC_OPACITY) {
                        lv_obj_set_style_opa(target, intValue, 0);
                    } else if (specific->property == DROPDOWN_SELECTED) {
                        lv_dropdown_set_selected(target, intValue);
                    } else if (specific->property == IMAGE_ANGLE) {
                        lv_img_set_angle(target, intValue);
                    } else if (specific->property == IMAGE_ZOOM) {
                        lv_img_set_zoom(target, intValue);
                    } else if (specific->property == ROLLER_SELECTED) {
                        lv_roller_set_selected(target, intValue, specific->animated ? LV_ANIM_ON : LV_ANIM_OFF);
                    } else if (specific->property == SLIDER_VALUE) {
                        lv_slider_set_value(target, intValue, specific->animated ? LV_ANIM_ON : LV_ANIM_OFF);
                    }
                }
            }
            lv_obj_update_layout(target);
        }
    }
    propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
#else
namespace eez {
namespace flow {
void executeLVGLComponent(FlowState *flowState, unsigned componentIndex) {
    throwError(flowState, componentIndex, "Not implemented");
}
} 
} 
#endif
// -----------------------------------------------------------------------------
// flow/components/lvgl_user_widget.cpp
// -----------------------------------------------------------------------------
#if defined(EEZ_FOR_LVGL)
namespace eez {
namespace flow {
struct LVGLUserWidgetComponent : public Component {
	int16_t flowIndex;
	uint8_t inputsStartIndex;
	uint8_t outputsStartIndex;
    int32_t widgetStartIndex;
};
LVGLUserWidgetExecutionState *createUserWidgetFlowState(FlowState *flowState, unsigned userWidgetWidgetComponentIndex) {
    auto component = (LVGLUserWidgetComponent *)flowState->flow->components[userWidgetWidgetComponentIndex];
    auto userWidgetFlowState = initPageFlowState(flowState->assets, component->flowIndex, flowState, userWidgetWidgetComponentIndex);
    userWidgetFlowState->lvglWidgetStartIndex = component->widgetStartIndex;
    auto userWidgetWidgetExecutionState = allocateComponentExecutionState<LVGLUserWidgetExecutionState>(flowState, userWidgetWidgetComponentIndex);
    userWidgetWidgetExecutionState->flowState = userWidgetFlowState;
    return userWidgetWidgetExecutionState;
}
void executeLVGLUserWidgetComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (LVGLUserWidgetComponent *)flowState->flow->components[componentIndex];
    auto userWidgetWidgetExecutionState = (LVGLUserWidgetExecutionState *)flowState->componenentExecutionStates[componentIndex];
    if (!userWidgetWidgetExecutionState) {
        userWidgetWidgetExecutionState = createUserWidgetFlowState(flowState, componentIndex);
    }
    auto userWidgetFlowState = userWidgetWidgetExecutionState->flowState;
    for (
        unsigned userWidgetComponentIndex = 0;
        userWidgetComponentIndex < userWidgetFlowState->flow->components.count;
        userWidgetComponentIndex++
    ) {
        auto userWidgetComponent = userWidgetFlowState->flow->components[userWidgetComponentIndex];
        if (userWidgetComponent->type == defs_v3::COMPONENT_TYPE_INPUT_ACTION) {
            auto inputActionComponentExecutionState = (InputActionComponentExecutionState *)userWidgetFlowState->componenentExecutionStates[userWidgetComponentIndex];
            if (inputActionComponentExecutionState) {
                Value value;
                if (getCallActionValue(userWidgetFlowState, userWidgetComponentIndex, value)) {
                    if (inputActionComponentExecutionState->value != value) {
                        addToQueue(userWidgetWidgetExecutionState->flowState, userWidgetComponentIndex, -1, -1, -1, false);
                        inputActionComponentExecutionState->value = value;
                    }
                } else {
                    return;
                }
            }
        } else if (userWidgetComponent->type == defs_v3::COMPONENT_TYPE_START_ACTION) {
            Value value;
            if (getCallActionValue(userWidgetFlowState, userWidgetComponentIndex, value)) {
                if (value.getType() != VALUE_TYPE_UNDEFINED) {
                    addToQueue(userWidgetWidgetExecutionState->flowState, userWidgetComponentIndex, -1, -1, -1, false);
                }
            } else {
                return;
            }
        }
    }
}
} 
} 
#else
namespace eez {
namespace flow {
void executeLVGLUserWidgetComponent(FlowState *flowState, unsigned componentIndex) {
    throwError(flowState, componentIndex, "Not implemented");
}
} 
} 
#endif
// -----------------------------------------------------------------------------
// flow/components/mqtt.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
namespace eez {
namespace flow {
struct MQTTEventActionComponenent : public Component {
    int16_t connectEventOutputIndex;
    int16_t reconnectEventOutputIndex;
    int16_t closeEventOutputIndex;
    int16_t disconnectEventOutputIndex;
    int16_t offlineEventOutputIndex;
    int16_t endEventOutputIndex;
    int16_t errorEventOutputIndex;
    int16_t messageEventOutputIndex;
};
struct MQTTEvent {
    int16_t outputIndex;
    Value value;
    MQTTEvent *next;
};
struct MQTTEventActionComponenentExecutionState : public ComponenentExecutionState {
	FlowState *flowState;
    unsigned componentIndex;
    MQTTEvent *firstEvent;
    MQTTEvent *lastEvent;
    MQTTEventActionComponenentExecutionState() : firstEvent(nullptr), lastEvent(nullptr) {}
    virtual ~MQTTEventActionComponenentExecutionState() override;
    void addEvent(int16_t outputIndex, Value value = Value(VALUE_TYPE_NULL)) {
        auto event = ObjectAllocator<MQTTEvent>::allocate(0xe1b95933);
        event->outputIndex = outputIndex;
        event->value = value;
        event->next = nullptr;
        if (!firstEvent) {
            firstEvent = event;
            lastEvent = event;
        } else {
            lastEvent->next = event;
            lastEvent = event;
        }
    }
    MQTTEvent *removeEvent() {
        auto event = firstEvent;
        if (event) {
            firstEvent = event->next;
            if (!firstEvent) {
                lastEvent = nullptr;
            }
        }
        return event;
    }
};
struct MQTTConnectionEventHandler {
    MQTTEventActionComponenentExecutionState *componentExecutionState;
    MQTTConnectionEventHandler *next;
    MQTTConnectionEventHandler *prev;
};
struct MQTTConnection {
    void *handle;
    MQTTConnectionEventHandler *firstEventHandler;
    MQTTConnectionEventHandler *lastEventHandler;
    MQTTConnection *next;
    MQTTConnection *prev;
};
MQTTConnection *g_firstMQTTConnection = nullptr;
MQTTConnection *g_lastMQTTConnection = nullptr;
static MQTTConnection *addConnection(void *handle) {
    auto connection = ObjectAllocator<MQTTConnection>::allocate(0x95d9f5d1);
    if (!connection) {
        return nullptr;
    }
    connection->handle = handle;
    connection->firstEventHandler = nullptr;
    connection->lastEventHandler = nullptr;
    if (!g_firstMQTTConnection) {
        g_firstMQTTConnection = connection;
        g_lastMQTTConnection = connection;
        connection->prev = nullptr;
        connection->next = nullptr;
    } else {
        g_lastMQTTConnection->next = connection;
        connection->prev = g_lastMQTTConnection;
        connection->next = nullptr;
        g_lastMQTTConnection = connection;
    }
    return connection;
}
static MQTTConnection *findConnection(void *handle) {
    for (auto connection = g_firstMQTTConnection; connection; connection = connection->next) {
        if (connection->handle == handle) {
            return connection;
        }
    }
    return nullptr;
}
static void deleteConnection(void *handle) {
    auto connection = findConnection(handle);
    if (!connection) {
        return;
    }
    while (connection->firstEventHandler) {
        deallocateComponentExecutionState(
            connection->firstEventHandler->componentExecutionState->flowState,
            connection->firstEventHandler->componentExecutionState->componentIndex
        );
    }
    eez_mqtt_deinit(connection->handle);
    if (connection->prev) {
        connection->prev->next = connection->next;
    } else {
        g_firstMQTTConnection = connection->next;
    }
    if (connection->next) {
        connection->next->prev = connection->prev;
    } else {
        g_lastMQTTConnection = connection->prev;
    }
    ObjectAllocator<MQTTConnection>::deallocate(connection);
}
MQTTConnectionEventHandler *addConnectionEventHandler(void *handle, MQTTEventActionComponenentExecutionState *componentExecutionState) {
    auto connection = findConnection(handle);
    if (!connection) {
        return nullptr;
    }
    auto eventHandler = ObjectAllocator<MQTTConnectionEventHandler>::allocate(0x75ccf1eb);
    if (!eventHandler) {
        return nullptr;
    }
    eventHandler->componentExecutionState = componentExecutionState;
    if (!connection->firstEventHandler) {
        connection->firstEventHandler = eventHandler;
        connection->lastEventHandler = eventHandler;
        eventHandler->prev = nullptr;
        eventHandler->next = nullptr;
    } else {
        connection->lastEventHandler->next = eventHandler;
        eventHandler->prev = connection->lastEventHandler;
        eventHandler->next = nullptr;
        connection->lastEventHandler = eventHandler;
    }
    return eventHandler;
}
static void removeEventHandler(MQTTEventActionComponenentExecutionState *componentExecutionState) {
    for (auto connection = g_firstMQTTConnection; connection; connection = connection->next) {
        for (auto eventHandler = connection->firstEventHandler; eventHandler; eventHandler = eventHandler->next) {
            if (eventHandler->componentExecutionState == componentExecutionState) {
                if (eventHandler->prev) {
                    eventHandler->prev->next = eventHandler->next;
                } else {
                    connection->firstEventHandler = eventHandler->next;
                }
                if (eventHandler->next) {
                    eventHandler->next->prev = eventHandler->prev;
                } else {
                    connection->lastEventHandler = eventHandler->prev;
                }
                ObjectAllocator<MQTTConnectionEventHandler>::deallocate(eventHandler);
                return;
            }
        }
    }
}
void eez_mqtt_on_event_callback(void *handle, EEZ_MQTT_Event event, void *eventData) {
    auto connection = findConnection(handle);
    if (!connection) {
        return;
    }
    for (auto eventHandler = connection->firstEventHandler; eventHandler; eventHandler = eventHandler->next) {
        auto componentExecutionState = eventHandler->componentExecutionState;
        auto flowState = componentExecutionState->flowState;
        auto componentIndex = componentExecutionState->componentIndex;
        auto component = (MQTTEventActionComponenent *)flowState->flow->components[componentIndex];
        if (event == EEZ_MQTT_EVENT_CONNECT) {
            if (component->connectEventOutputIndex >= 0) {
                componentExecutionState->addEvent(component->connectEventOutputIndex);
            }
        } else if (event == EEZ_MQTT_EVENT_RECONNECT) {
            if (component->reconnectEventOutputIndex >= 0) {
                componentExecutionState->addEvent(component->reconnectEventOutputIndex);
            }
        } else if (event == EEZ_MQTT_EVENT_CLOSE) {
            if (component->closeEventOutputIndex >= 0) {
                componentExecutionState->addEvent(component->closeEventOutputIndex);
            }
        } else if (event == EEZ_MQTT_EVENT_DISCONNECT) {
            if (component->disconnectEventOutputIndex >= 0) {
                componentExecutionState->addEvent(component->disconnectEventOutputIndex);
            }
        } else if (event == EEZ_MQTT_EVENT_OFFLINE) {
            if (component->offlineEventOutputIndex >= 0) {
                componentExecutionState->addEvent(component->offlineEventOutputIndex);
            }
        } else if (event == EEZ_MQTT_EVENT_END) {
            if (component->endEventOutputIndex >= 0) {
                componentExecutionState->addEvent(component->endEventOutputIndex);
            }
        } else if (event == EEZ_MQTT_EVENT_ERROR) {
            if (component->errorEventOutputIndex >= 0) {
                componentExecutionState->addEvent(component->errorEventOutputIndex, Value::makeStringRef((const char *)eventData, -1, 0x2b7ac31a));
            }
        } else if (event == EEZ_MQTT_EVENT_MESSAGE) {
            if (component->messageEventOutputIndex >= 0) {
                auto messageEvent = (EEZ_MQTT_MessageEvent *)eventData;
                Value messageValue = Value::makeArrayRef(defs_v3::SYSTEM_STRUCTURE_MQTT_MESSAGE_NUM_FIELDS, defs_v3::SYSTEM_STRUCTURE_MQTT_MESSAGE, 0xe256716a);
                auto messageArray = messageValue.getArray();
                messageArray->values[defs_v3::SYSTEM_STRUCTURE_MQTT_MESSAGE_FIELD_TOPIC] = Value::makeStringRef(messageEvent->topic, -1, 0x5bdff567);
                messageArray->values[defs_v3::SYSTEM_STRUCTURE_MQTT_MESSAGE_FIELD_PAYLOAD] = Value::makeStringRef(messageEvent->payload, -1, 0xcfa25e4f);
                componentExecutionState->addEvent(component->messageEventOutputIndex, messageValue);
            }
        }
    }
}
void onFreeMQTTConnection(ArrayValue *mqttConnectionValue) {
    void *handle = mqttConnectionValue->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_ID].getVoidPointer();
    deleteConnection(handle);
}
MQTTEventActionComponenentExecutionState::~MQTTEventActionComponenentExecutionState() {
    removeEventHandler(this);
    while (firstEvent) {
        auto event = removeEvent();
        ObjectAllocator<MQTTEvent>::deallocate(event);
    }
}
void executeMQTTInitComponent(FlowState *flowState, unsigned componentIndex) {
    Value connectionDstValue;
    if (!evalAssignableProperty(flowState, componentIndex, defs_v3::MQTT_INIT_ACTION_COMPONENT_PROPERTY_CONNECTION, connectionDstValue, "Failed to evaluate Connection in MQTTInit")) {
        return;
    }
    Value protocolValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_INIT_ACTION_COMPONENT_PROPERTY_PROTOCOL, protocolValue, "Failed to evaluate Protocol in MQTTInit")) {
        return;
    }
    if (!protocolValue.isString()) {
        throwError(flowState, componentIndex, "Protocol must be a string");
        return;
    }
    Value hostValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_INIT_ACTION_COMPONENT_PROPERTY_HOST, hostValue, "Failed to evaluate Host in MQTTInit")) {
        return;
    }
    if (!hostValue.isString()) {
        throwError(flowState, componentIndex, "Host must be a string");
        return;
    }
    Value portValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_INIT_ACTION_COMPONENT_PROPERTY_PORT, portValue, "Failed to evaluate Port in MQTTInit")) {
        return;
    }
    if (portValue.getType() != VALUE_TYPE_INT32) {
        throwError(flowState, componentIndex, "Port must be an integer");
        return;
    }
    Value usernameValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_INIT_ACTION_COMPONENT_PROPERTY_USER_NAME, usernameValue, "Failed to evaluate Username in MQTTInit")) {
        return;
    }
    if (usernameValue.getType() != VALUE_TYPE_UNDEFINED && !usernameValue.isString()) {
        throwError(flowState, componentIndex, "Username must be a string");
        return;
    }
    Value passwordValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_INIT_ACTION_COMPONENT_PROPERTY_PASSWORD, passwordValue, "Failed to evaluate Password in MQTTInit")) {
        return;
    }
    if (passwordValue.getType() != VALUE_TYPE_UNDEFINED && !passwordValue.isString()) {
        throwError(flowState, componentIndex, "Password must be a string");
        return;
    }
    void *handle;
    auto result = eez_mqtt_init(protocolValue.getString(), hostValue.getString(), portValue.getInt32(), usernameValue.getString(), passwordValue.getString(), &handle);
    if (result != MQTT_ERROR_OK) {
        char errorMessage[256];
        snprintf(errorMessage, sizeof(errorMessage), "Failed to initialize MQTT connection with error code: %d", (int)result);
        throwError(flowState, componentIndex, errorMessage);
        return;
    }
    addConnection(handle);
    Value connectionValue = Value::makeArrayRef(defs_v3::OBJECT_TYPE_MQTT_CONNECTION_NUM_FIELDS, defs_v3::OBJECT_TYPE_MQTT_CONNECTION, 0x51ba2203);
    auto connectionArray = connectionValue.getArray();
    connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_PROTOCOL] = protocolValue;
    connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_HOST] = hostValue;
    connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_PORT] = portValue;
    connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_USER_NAME] = usernameValue;
    connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_PASSWORD] = passwordValue;
    connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_IS_CONNECTED] = Value(false, VALUE_TYPE_BOOLEAN);
    connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_ID] = Value(handle, VALUE_TYPE_POINTER);
    Value statusValue = Value::makeArrayRef(defs_v3::SYSTEM_STRUCTURE_OBJECT_VARIABLE_STATUS_NUM_FIELDS, defs_v3::SYSTEM_STRUCTURE_OBJECT_VARIABLE_STATUS, 0x51ba2203);
    auto statusArray = statusValue.getArray();
    statusArray->values[defs_v3::SYSTEM_STRUCTURE_OBJECT_VARIABLE_STATUS_FIELD_LABEL] = Value("", VALUE_TYPE_STRING);
    statusArray->values[defs_v3::SYSTEM_STRUCTURE_OBJECT_VARIABLE_STATUS_FIELD_IMAGE] = Value("data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABgAAAAYCAYAAADgdz34AAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsIAAA7CARUoSoAAAAItSURBVEhL1dZNSBVRGMbxmcjCiFz14aJVlKtcVFjUJghCIlq5aBHoQiwoyJaRCzdRUUhQUIuIyKiEdoLuolCQEgQzyk8KjTZhidEnwe3/nJl3ermc8m5c9MCPmTtn5rz3zMw596alUilZzqzIt8uWYgRpmp5ncxQ/4YelL/EJ9/AQ87CkOIMT4VOS/MIq9NDvuXBEBfIiL/VxCW/Qjmr4tMG+mMwU/boCW7ETB3EKdzEFu8h7jr3w0XUandrHYwViWYdDeAT/DeUbTsPnAL5jOlZAw9b9+1v2oB++iFyGzzFER9CHEfTiAhpRg/KcxGf4Ilfh0xIrMAN/kUyjE7Xw2Yc5+HM7UCRWQJ35C7x3aIVPPWbhzzuCkFgBPczj6MIQyh+q3MEaWBqgOWLteo03IVrAR5NrF26jvJCe1VpYmuHbr2PJAj778Qq+kx74ZeYBrO0r6mMF9CbcQAs264CLHvIgrBM5C8s2LMDabsUK6P7ZCR9wCf413YgXsHMWsR2Wa7C2iViBcXeCGcYWWHbjC6y9G5Yd0CzW8ehEixWQUWyA5QqsTROuDopW1gHoeFGgkt8Dve8Xs92Qm9DtUfQ2Hc52Q8ePs90/qfQHR+uLXltFE/JpthuiBc7yLN9qNCGVFqhCU7Yb8iTfKrpFWnWVt/iBaAF18q9o/bFM5FtFr/D6bDf5CBUoVmVf4D40fD3s12V0XPd9NZT30OSbzK2Eokk2Bk28kP/9X0WS/AaVCm1sgeHGuwAAAABJRU5ErkJggg==", VALUE_TYPE_STRING);
    statusArray->values[defs_v3::SYSTEM_STRUCTURE_OBJECT_VARIABLE_STATUS_FIELD_COLOR] = Value("gray", VALUE_TYPE_STRING);
    statusArray->values[defs_v3::SYSTEM_STRUCTURE_OBJECT_VARIABLE_STATUS_FIELD_ERROR] = Value();
    connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_STATUS] = statusValue;
    assignValue(flowState, componentIndex, connectionDstValue, connectionValue);
	propagateValueThroughSeqout(flowState, componentIndex);
}
void executeMQTTConnectComponent(FlowState *flowState, unsigned componentIndex) {
    Value connectionValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_CONNECT_ACTION_COMPONENT_PROPERTY_CONNECTION, connectionValue, "Failed to evaluate Connection in MQTTEvent")) {
        return;
    }
    if (!connectionValue.isArray() || connectionValue.getArray()->arrayType != defs_v3::OBJECT_TYPE_MQTT_CONNECTION) {
        throwError(flowState, componentIndex, "Connection must be a object:MQTTConnection");
        return;
    }
    auto connectionArray = connectionValue.getArray();
    void *handle = connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_ID].getVoidPointer();
    auto result = eez_mqtt_connect(handle);
    if (result != MQTT_ERROR_OK) {
        char errorMessage[256];
        snprintf(errorMessage, sizeof(errorMessage), "Failed to connect to MQTT broker with error code: %d", (int)result);
        throwError(flowState, componentIndex, errorMessage);
        return;
    }
	propagateValueThroughSeqout(flowState, componentIndex);
}
void executeMQTTDisconnectComponent(FlowState *flowState, unsigned componentIndex) {
    Value connectionValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_DISCONNECT_ACTION_COMPONENT_PROPERTY_CONNECTION, connectionValue, "Failed to evaluate Connection in MQTTDisconnect")) {
        return;
    }
    if (!connectionValue.isArray() || connectionValue.getArray()->arrayType != defs_v3::OBJECT_TYPE_MQTT_CONNECTION) {
        throwError(flowState, componentIndex, "Connection must be a object:MQTTConnection");
        return;
    }
    auto connectionArray = connectionValue.getArray();
    void *handle = connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_ID].getVoidPointer();
    auto result = eez_mqtt_disconnect(handle);
    if (result != MQTT_ERROR_OK) {
        char errorMessage[256];
        snprintf(errorMessage, sizeof(errorMessage), "Failed to disconnect from MQTT broker with error code: %d", (int)result);
        throwError(flowState, componentIndex, errorMessage);
        return;
    }
	propagateValueThroughSeqout(flowState, componentIndex);
}
void executeMQTTEventComponent(FlowState *flowState, unsigned componentIndex) {
    Value connectionValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_EVENT_ACTION_COMPONENT_PROPERTY_CONNECTION, connectionValue, "Failed to evaluate Connection in MQTTEvent")) {
        return;
    }
    if (!connectionValue.isArray() || connectionValue.getArray()->arrayType != defs_v3::OBJECT_TYPE_MQTT_CONNECTION) {
        throwError(flowState, componentIndex, "Connection must be a object:MQTTConnection");
        return;
    }
    auto componentExecutionState = (MQTTEventActionComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
    if (!componentExecutionState) {
        componentExecutionState = allocateComponentExecutionState<MQTTEventActionComponenentExecutionState>(flowState, componentIndex);
        componentExecutionState->flowState = flowState;
        componentExecutionState->componentIndex = componentIndex;
        auto connectionArray = connectionValue.getArray();
        void *handle = connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_ID].getVoidPointer();
        addConnectionEventHandler(handle, componentExecutionState);
	    propagateValueThroughSeqout(flowState, componentIndex);
        addToQueue(flowState, componentIndex, -1, -1, -1, true);
    } else {
        auto event = componentExecutionState->removeEvent();
        if (event) {
            propagateValue(flowState, componentIndex, event->outputIndex, event->value);
            ObjectAllocator<MQTTEvent>::deallocate(event);
        } else {
            addToQueue(flowState, componentIndex, -1, -1, -1, true);
        }
    }
}
void executeMQTTSubscribeComponent(FlowState *flowState, unsigned componentIndex) {
    Value connectionValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_SUBSCRIBE_ACTION_COMPONENT_PROPERTY_CONNECTION, connectionValue, "Failed to evaluate Connection in MQTTSubscribe")) {
        return;
    }
    if (!connectionValue.isArray() || connectionValue.getArray()->arrayType != defs_v3::OBJECT_TYPE_MQTT_CONNECTION) {
        throwError(flowState, componentIndex, "Connection must be a object:MQTTConnection");
        return;
    }
    Value topicValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_SUBSCRIBE_ACTION_COMPONENT_PROPERTY_TOPIC, topicValue, "Failed to evaluate Topic in MQTTSubscribe")) {
        return;
    }
    if (!topicValue.isString()) {
        throwError(flowState, componentIndex, "Topic must be a string");
        return;
    }
    auto connectionArray = connectionValue.getArray();
    void *handle = connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_ID].getVoidPointer();
    auto result = eez_mqtt_subscribe(handle, topicValue.getString());
    if (result != MQTT_ERROR_OK) {
        char errorMessage[256];
        snprintf(errorMessage, sizeof(errorMessage), "Failed to subscribe to MQTT topic with error code: %d", (int)result);
        throwError(flowState, componentIndex, errorMessage);
        return;
    }
    propagateValueThroughSeqout(flowState, componentIndex);
}
void executeMQTTUnsubscribeComponent(FlowState *flowState, unsigned componentIndex) {
    Value connectionValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_UNSUBSCRIBE_ACTION_COMPONENT_PROPERTY_CONNECTION, connectionValue, "Failed to evaluate Connection in MQTTUnsubscribe")) {
        return;
    }
    if (!connectionValue.isArray() || connectionValue.getArray()->arrayType != defs_v3::OBJECT_TYPE_MQTT_CONNECTION) {
        throwError(flowState, componentIndex, "Connection must be a object:MQTTConnection");
        return;
    }
    Value topicValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_UNSUBSCRIBE_ACTION_COMPONENT_PROPERTY_TOPIC, topicValue, "Failed to evaluate Topic in MQTTUnsubscribe")) {
        return;
    }
    if (!topicValue.isString()) {
        throwError(flowState, componentIndex, "Topic must be a string");
        return;
    }
    auto connectionArray = connectionValue.getArray();
    void *handle = connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_ID].getVoidPointer();
    auto result = eez_mqtt_unsubscribe(handle, topicValue.getString());
    if (result != MQTT_ERROR_OK) {
        char errorMessage[256];
        snprintf(errorMessage, sizeof(errorMessage), "Failed to unsubscribe from MQTT topic with error code: %d", (int)result);
        throwError(flowState, componentIndex, errorMessage);
        return;
    }
    propagateValueThroughSeqout(flowState, componentIndex);
}
void executeMQTTPublishComponent(FlowState *flowState, unsigned componentIndex) {
	Value connectionValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_PUBLISH_ACTION_COMPONENT_PROPERTY_CONNECTION, connectionValue, "Failed to evaluate Connection in MQTTPublish")) {
        return;
    }
    if (!connectionValue.isArray() || connectionValue.getArray()->arrayType != defs_v3::OBJECT_TYPE_MQTT_CONNECTION) {
        throwError(flowState, componentIndex, "Connection must be a object:MQTTConnection");
        return;
    }
    Value topicValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_PUBLISH_ACTION_COMPONENT_PROPERTY_TOPIC, topicValue, "Failed to evaluate Topic in MQTTPublish")) {
        return;
    }
    if (!topicValue.isString()) {
        throwError(flowState, componentIndex, "Topic must be a string");
        return;
    }
    Value payloadValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::MQTT_PUBLISH_ACTION_COMPONENT_PROPERTY_PAYLOAD, payloadValue, "Failed to evaluate Payload in MQTTPublish")) {
        return;
    }
    if (!payloadValue.isString()) {
        throwError(flowState, componentIndex, "Topic must be a string");
        return;
    }
    auto connectionArray = connectionValue.getArray();
    void *handle = connectionArray->values[defs_v3::OBJECT_TYPE_MQTT_CONNECTION_FIELD_ID].getVoidPointer();
    auto result = eez_mqtt_publish(handle, topicValue.getString(), payloadValue.getString());
    if (result != MQTT_ERROR_OK) {
        char errorMessage[256];
        snprintf(errorMessage, sizeof(errorMessage), "Failed to subscribe to MQTT topic with error code: %d", (int)result);
        throwError(flowState, componentIndex, errorMessage);
        return;
    }
    propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
#ifdef EEZ_STUDIO_FLOW_RUNTIME
#include <emscripten.h>
extern "C" {
int eez_mqtt_init(const char *protocol, const char *host, int port, const char *username, const char *password, void **handle) {
    int id = EM_ASM_INT({
        return eez_mqtt_init($0, UTF8ToString($1), UTF8ToString($2), $3, UTF8ToString($4), UTF8ToString($5));
    }, eez::flow::g_wasmModuleId, protocol, host, port, username, password);
    if (id == 0) {
        return 1;
    }
    *handle = (void *)id;
    return MQTT_ERROR_OK;
}
int eez_mqtt_deinit(void *handle) {
    return EM_ASM_INT({
        return eez_mqtt_deinit($0, $1);
    }, eez::flow::g_wasmModuleId, handle);
}
int eez_mqtt_connect(void *handle) {
    return EM_ASM_INT({
        return eez_mqtt_connect($0, $1);
    }, eez::flow::g_wasmModuleId, handle);
}
int eez_mqtt_disconnect(void *handle) {
    return EM_ASM_INT({
        return eez_mqtt_disconnect($0, $1);
    }, eez::flow::g_wasmModuleId, handle);
}
int eez_mqtt_subscribe(void *handle, const char *topic) {
    return EM_ASM_INT({
        return eez_mqtt_subscribe($0, $1, UTF8ToString($2));
    }, eez::flow::g_wasmModuleId, handle, topic);
}
int eez_mqtt_unsubscribe(void *handle, const char *topic) {
    return EM_ASM_INT({
        return eez_mqtt_unsubscribe($0, $1, UTF8ToString($2));
    }, eez::flow::g_wasmModuleId, handle, topic);
}
int eez_mqtt_publish(void *handle, const char *topic, const char *payload) {
    return EM_ASM_INT({
        return eez_mqtt_publish($0, $1, UTF8ToString($2), UTF8ToString($3));
    }, eez::flow::g_wasmModuleId, handle, topic, payload);
}
}
EM_PORT_API(void) onMqttEvent(void *handle, EEZ_MQTT_Event event, void *eventDataPtr1, void *eventDataPtr2) {
    void *eventData;
    if (eventDataPtr1 && eventDataPtr2)  {
        EEZ_MQTT_MessageEvent eventData;
        eventData.topic = (const char *)eventDataPtr1;
        eventData.payload = (const char *)eventDataPtr2;
        eez::flow::eez_mqtt_on_event_callback(handle, event, &eventData);
    } else if (eventDataPtr1) {
        eez::flow::eez_mqtt_on_event_callback(handle, event, eventDataPtr1);
    } else {
        eez::flow::eez_mqtt_on_event_callback(handle, event, nullptr);
    }
}
#else
#ifndef EEZ_MQTT_ADAPTER
int eez_mqtt_init(const char *protocol, const char *host, int port, const char *username, const char *password, void **handle) {
    return MQTT_ERROR_NOT_IMPLEMENTED;
}
int eez_mqtt_deinit(void *handle) {
    return MQTT_ERROR_NOT_IMPLEMENTED;
}
int eez_mqtt_connect(void *handle) {
    return MQTT_ERROR_NOT_IMPLEMENTED;
}
int eez_mqtt_disconnect(void *handle) {
    return MQTT_ERROR_NOT_IMPLEMENTED;
}
int eez_mqtt_subscribe(void *handle, const char *topic) {
    return MQTT_ERROR_NOT_IMPLEMENTED;
}
int eez_mqtt_unsubscribe(void *handle, const char *topic) {
    return MQTT_ERROR_NOT_IMPLEMENTED;
}
int eez_mqtt_publish(void *handle, const char *topic, const char *payload) {
    return MQTT_ERROR_NOT_IMPLEMENTED;
}
#endif
#endif
// -----------------------------------------------------------------------------
// flow/components/on_event.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeOnEventComponent(FlowState *flowState, unsigned componentIndex) {
    propagateValue(flowState, componentIndex, 1, flowState->eventValue);
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/noop.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeNoopComponent(FlowState *flowState, unsigned componentIndex) {
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/output.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
struct OutputActionComponent : public Component {
	uint8_t outputIndex;
};
void executeOutputComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (OutputActionComponent *)flowState->flow->components[componentIndex];
	if (!flowState->parentFlowState) {
		throwError(flowState, componentIndex, "No parentFlowState in Output\n");
		return;
	}
	if (!flowState->parentComponent) {
		throwError(flowState, componentIndex, "No parentComponent in Output\n");
		return;
	}
    auto inputIndex = component->inputs[0];
    if (inputIndex >= flowState->flow->componentInputs.count) {
        throwError(flowState, componentIndex, "Invalid input index in Output\n");
		return;
	}
    auto value = flowState->values[inputIndex];
    auto callActionComponent = (CallActionActionComponent *)flowState->parentComponent;
    uint8_t parentComponentOutputIndex = callActionComponent->outputsStartIndex + component->outputIndex;
    if (parentComponentOutputIndex >= flowState->parentComponent->outputs.count) {
        throwError(flowState, componentIndex, "Output action component, invalid output index\n");
		return;
    }
    propagateValue(flowState->parentFlowState, flowState->parentComponentIndex, parentComponentOutputIndex, value);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/select_language.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
namespace eez {
namespace flow {
void executeSelectLanguageComponent(FlowState *flowState, unsigned componentIndex) {
	Value languageValue;
	if (!evalProperty(flowState, componentIndex, defs_v3::SELECT_LANGUAGE_ACTION_COMPONENT_PROPERTY_LANGUAGE, languageValue, "Failed to evaluate Language in SelectLanguage")) {
		return;
	}
	const char *language = languageValue.getString();
    auto &languages = flowState->assets->languages;
    for (uint32_t languageIndex = 0; languageIndex < languages.count; languageIndex++) {
        if (strcmp(languages[languageIndex]->languageID, language) == 0) {
            g_selectedLanguage = languageIndex;
	        propagateValueThroughSeqout(flowState, componentIndex);
            return;
        }
    }
    char message[256];
    snprintf(message, sizeof(message), "Unknown language %s", language);
    throwError(flowState, componentIndex, message);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/set_variable.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
namespace eez {
namespace flow {
void executeSetVariableComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (SetVariableActionComponent *)flowState->flow->components[componentIndex];
    for (uint32_t entryIndex = 0; entryIndex < component->entries.count; entryIndex++) {
        auto entry = component->entries[entryIndex];
        char strErrorMessage[256];
        snprintf(strErrorMessage, sizeof(strErrorMessage), "Failed to evaluate Variable no. %d in SetVariable", (int)(entryIndex + 1));
        Value dstValue;
        if (!evalAssignableExpression(flowState, componentIndex, entry->variable, dstValue, strErrorMessage)) {
            return;
        }
        snprintf(strErrorMessage, sizeof(strErrorMessage), "Failed to evaluate Value no. %d in SetVariable", (int)(entryIndex + 1));
        Value srcValue;
        if (!evalExpression(flowState, componentIndex, entry->value, srcValue, strErrorMessage)) {
            return;
        }
        assignValue(flowState, componentIndex, dstValue, srcValue);
    }
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/show_page.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
struct ShowPageActionComponent : public Component {
	int16_t page;
};
void executeShowPageComponent(FlowState *flowState, unsigned componentIndex) {
	auto component = (ShowPageActionComponent *)flowState->flow->components[componentIndex];
	replacePageHook(component->page, 0, 0, 0);
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/sort_array.cpp
// -----------------------------------------------------------------------------
#include <string.h>
#include <stdlib.h>
namespace eez {
namespace flow {
SortArrayActionComponent *g_sortArrayActionComponent;
int elementCompare(const void *a, const void *b) {
    auto aValue = *(const Value *)a;
    auto bValue = *(const Value *)b;
    if (g_sortArrayActionComponent->arrayType != -1) {
        if (!aValue.isArray()) {
            return 0;
        }
        auto aArray = aValue.getArray();
        if ((uint32_t)g_sortArrayActionComponent->structFieldIndex >= aArray->arraySize) {
            return 0;
        }
        aValue = aArray->values[g_sortArrayActionComponent->structFieldIndex];
        if (!bValue.isArray()) {
            return 0;
        }
        auto bArray = bValue.getArray();
        if ((uint32_t)g_sortArrayActionComponent->structFieldIndex >= bArray->arraySize) {
            return 0;
        }
        bValue = bArray->values[g_sortArrayActionComponent->structFieldIndex];
    }
    int result;
    if (aValue.isString() && bValue.isString()) {
        if (g_sortArrayActionComponent->flags & SORT_ARRAY_FLAG_IGNORE_CASE) {
            result = utf8casecmp(aValue.getString(), bValue.getString());
        } else {
            result = utf8cmp(aValue.getString(), bValue.getString());
        }
    } else {
        int err;
        float aDouble = aValue.toDouble(&err);
        if (err) {
            return 0;
        }
        float bDouble = bValue.toDouble(&err);
        if (err) {
            return 0;
        }
        auto diff = aDouble - bDouble;
        result = diff < 0 ? -1 : diff > 0 ? 1 : 0;
    }
    if (!(g_sortArrayActionComponent->flags & SORT_ARRAY_FLAG_ASCENDING)) {
        result = -result;
    }
    return result;
}
void sortArray(SortArrayActionComponent *component, ArrayValue *array) {
    g_sortArrayActionComponent = component;
    qsort(&array->values[0], array->arraySize, sizeof(Value), elementCompare);
}
void executeSortArrayComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (SortArrayActionComponent *)flowState->flow->components[componentIndex];
    Value srcArrayValue;
    if (!evalProperty(flowState, componentIndex, defs_v3::SORT_ARRAY_ACTION_COMPONENT_PROPERTY_ARRAY, srcArrayValue, "Failed to evaluate Array in SortArray\n")) {
        return;
    }
    if (!srcArrayValue.isArray()) {
        throwError(flowState, componentIndex, "SortArray: not an array\n");
        return;
    }
    auto arrayValue = srcArrayValue.clone();
    auto array = arrayValue.getArray();
    if (component->arrayType != -1) {
        if (array->arrayType != (uint32_t)component->arrayType) {
            throwError(flowState, componentIndex, "SortArray: invalid array type\n");
            return;
        }
        if (component->structFieldIndex < 0) {
            throwError(flowState, componentIndex, "SortArray: invalid struct field index\n");
        }
    } else {
        if (array->arrayType != defs_v3::ARRAY_TYPE_INTEGER && array->arrayType != defs_v3::ARRAY_TYPE_FLOAT && array->arrayType != defs_v3::ARRAY_TYPE_DOUBLE && array->arrayType != defs_v3::ARRAY_TYPE_STRING) {
            throwError(flowState, componentIndex, "SortArray: array type is neither array:integer or array:float or array:double or array:string\n");
            return;
        }
    }
    sortArray(component, array);
	propagateValue(flowState, componentIndex, component->outputs.count - 1, arrayValue);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/switch.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
namespace eez {
namespace flow {
void executeSwitchComponent(FlowState *flowState, unsigned componentIndex) {
    auto component = (SwitchActionComponent *)flowState->flow->components[componentIndex];
    for (uint32_t testIndex = 0; testIndex < component->tests.count; testIndex++) {
        auto test = component->tests[testIndex];
        char strMessage[256];
        snprintf(strMessage, sizeof(strMessage), "Failed to evaluate test condition no. %d in Switch", (int)(testIndex + 1));
        Value conditionValue;
        if (!evalExpression(flowState, componentIndex, test->condition, conditionValue, strMessage)) {
            return;
        }
        int err;
        bool result = conditionValue.toBool(&err);
        if (err) {
            char strMessage[256];
            snprintf(strMessage, sizeof(strMessage), "Failed to convert test condition no. %d to boolean in Switch\n", (int)(testIndex + 1));
            throwError(flowState, componentIndex, strMessage);
            return;
        }
        if (result) {
            snprintf(strMessage, sizeof(strMessage), "Failed to evaluate test output value no. %d in Switch", (int)(testIndex + 1));
            Value outputValue;
            if (!evalExpression(flowState, componentIndex, test->outputValue, outputValue, strMessage)) {
                return;
            }
            propagateValue(flowState, componentIndex, test->outputIndex, outputValue);
            break;
        }
    }
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/start.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeStartComponent(FlowState *flowState, unsigned componentIndex) {
	propagateValueThroughSeqout(flowState, componentIndex);
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/test_and_set.cpp
// -----------------------------------------------------------------------------
namespace eez {
namespace flow {
void executeTestAndSetComponent(FlowState *flowState, unsigned componentIndex) {
    Value dstValue;
    if (!evalAssignableProperty(flowState, componentIndex, defs_v3::TEST_AND_SET_ACTION_COMPONENT_PROPERTY_VARIABLE, dstValue, "Failed to evaluate Variable in TestAndSet")) {
        return;
    }
    if (dstValue.getValue().type != VALUE_TYPE_BOOLEAN) {
        throwError(flowState, componentIndex, "Variable in TestAndSet must be of type Boolean");
        return;
    }
    if (!dstValue.getValue().getBoolean()) {
        assignValue(flowState, componentIndex, dstValue, Value(true, VALUE_TYPE_BOOLEAN));
        propagateValueThroughSeqout(flowState, componentIndex);
    } else {
        addToQueue(flowState, componentIndex, -1, -1, -1, true);
    }
}
} 
} 
// -----------------------------------------------------------------------------
// flow/components/watch_variable.cpp
// -----------------------------------------------------------------------------
#include <stdio.h>
namespace eez {
namespace flow {
struct WatchVariableComponenentExecutionState : public ComponenentExecutionState {
	Value value;
    WatchListNode *node;
};
void executeWatchVariableComponent(FlowState *flowState, unsigned componentIndex) {
	auto watchVariableComponentExecutionState = (WatchVariableComponenentExecutionState *)flowState->componenentExecutionStates[componentIndex];
    Value value;
    if (!evalProperty(flowState, componentIndex, defs_v3::WATCH_VARIABLE_ACTION_COMPONENT_PROPERTY_VARIABLE, value, "Failed to evaluate Variable in WatchVariable")) {
        return;
    }
	if (!watchVariableComponentExecutionState) {
        watchVariableComponentExecutionState = allocateComponentExecutionState<WatchVariableComponenentExecutionState>(flowState, componentIndex);
        watchVariableComponentExecutionState->value = value;
        watchVariableComponentExecutionState->node = watchListAdd(flowState, componentIndex);
        propagateValue(flowState, componentIndex, 1, value);
	} else {
		if (value != watchVariableComponentExecutionState->value) {
            watchVariableComponentExecutionState->value = value;
			propagateValue(flowState, componentIndex, 1, value);
		}
	}
}
} 
} 