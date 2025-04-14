/*
   Copyright 2007 oasisfeng
   http://fontrouter.oasisfeng.com/

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef __FONTROUTER_INCLUDED__
#define __FONTROUTER_INCLUDED__

#ifdef __WIN32__
#define __attribute__(unused)
#endif

#pragma warning(disable: 4100)
#include <e32uid.h>
#include <openfont.h>
#include <openfontconstants.h>
#include <openfontrasterizer.h>
#include <openfontlinkedtypefaceextension.h>
#include <fntstore.h>
#include <flogger.h>
#include <e32std.h>
#include <BADESCA.H>		// CDesC16ArrayFlat
#include "CSettings.hpp"
#include "DebugHelper.hpp"


#define UNUSED_ARG(arg) (arg) = (arg)
#define TRAPC(_t,_r,_s) {if (_t.Trap(_r)==0){_s;TTrap::UnTrap();}}


// Version information
const TUint8 	KMajorVersion				= 2;
const TUint8	KMinorVersion				= 0;
const TUint32 	KBuildVersion				= 2006071301;

// Important preferences
const TInt KUidFontRouter					= 0x20009F34;
const TInt KLineBufferSize					= 1024;		// Max line length for INI file.

// Const definitions
const TInt KUnicodeNullChar					= 0xFFFE;	// Character never be available in this unicode.
const TInt KDefaultKPixelHeightInTwips		= 11860;	// Default height in twips of 1000 pixels.

_LIT(KFreeTypeDllPath,		"E:\\System\\Fonts\\Chinese\\");
_LIT(KFreeTypeDllFile,		"FreeType.dll");
_LIT(KTrueTypeFontFile,		"E:\\System\\Fonts\\Chinese\\Arial.ttf");

_LIT(KFontNameWildcard,			"*");


class TOpenFontFaceAttribEx;

enum TFontSpecMark
{
	EFontSpecNew = 3668768837,			// New font spec which is not preprocessed yet.
	EFontSpecNormal,					// Font spec to be processed normally, without preprocess.
	EFontSpecByPass 					// Font spec to be bypassed.
};



//////////////////////////////////////////////////////////////////////////
// RSettings
class RSettings : public MIniParser
{
public:
	enum TNativeFontAdaption
	{
		EBypassNativeFont,
		EReserveAndProxyNativeFont,
		EProxyNativeFont
	};
	enum TBitmapTypeAtt		// Must comply to: 	PreferXXX = 2 * TGlyphBitmapType::EXXX - 1.
	{						//				ForceXXX = 2 * TGlyphBitmapType::EXXX.
		ENoChange,
		EPreferMonochrome,
		EForceMonochrome,
		EPreferAntiAliased,
		EForceAntiAliased,
		EPreferSubPixel,
		EForceSubPixel,
		EBitmapTypeAttButt
	};
	enum TFontAtt
	{
		EFontAttNone = 0,

		// 3-states attributes
		EFontAttBoldOnDemand = 1,		// b
		EFontAttBoldAlways = 2,			// B

		EFontAttUpright = 4,			// i
		EFontAttItalic = 8,				// I

		EFontAttMonochrome = 16,		// a
		EFontAttAntiAliased = 32,		// A

		EFontAttHeightUniform = 64,		// e
		EFontAttHeightExpand = 128,		// E

		// Complex attributes
		EFontAttYAdjust = 256,			// Y<n>
		EFontAttCharGapAdjust = 512,	// W<n>
		EFontAttLineGapAdjust = 1024,	// L<n>
		EFontAttChromaAdjust = 2048,	// C<n>
		EFontAttZoom = 4096,			// Z<n>

		// Combination of all values for matching.
		EFontAttAll = EFontAttUpright | EFontAttItalic | EFontAttBoldOnDemand | EFontAttBoldAlways
			| EFontAttMonochrome | EFontAttAntiAliased
	};
	enum TFixFontMetricsMode
	{
		EFixFontMetricsNone,
		EFixFontMetricsNoDescent,
		EFixFontMetricsButt
	};
	enum TFixCharMetricsMode
	{
		EFixCharMetricsNone,
		EFixCharMetricsAuto,
		EFixCharMetricsButt
	};
	class TRoutedFontAtt		// Additional attributes for routed font
	{
	public:
		TInt8 iYAdjust;
		TInt8 iCharGapAdjust;
		TInt8 iLineGapAdjust;
		TInt8 iReserved; // padding
		TUint iChromaAdjust;
		TUint iZoomRatio;
	};
	class TFontMapEntry
	{
	public:
		inline TFontMapEntry()
		{
			Mem::FillZ(this, sizeof(*this));
			iDesiredFontHeight = iRoutedFontHeight = KErrNotFound;
			iDesiredFontAttr = EFontAttAll;
		}
		inline void ResetRoutedFontInfo()		// Reset routed font info only.
		{
			Mem::FillZ(&iRoutedFontName, sizeof(*this) - ((TUint8*)&iRoutedFontName - (TUint8*)this));
			iRoutedFontHeight = KErrNotFound;
		}
		inline TBool IsUltimateWild() const
		{
			return (iDesiredFontName == KFontNameWildcard && iDesiredFontHeight == KErrNotFound && iDesiredFontAttr == EFontAttAll
				&& iRoutedFontName == KFontNameWildcard && iRoutedFontHeight == KErrNotFound && iRoutedFontAttr == 0);
		}

		TPtrC iDesiredFontName;
		TInt iDesiredFontHeight;
		TInt iDesiredFontAttr;	// TFontAtt

		TPtrC iRoutedFontName;
		TInt iRoutedFontHeight;
		TInt iRoutedFontAttr;	// TFontAtt
		TRoutedFontAtt iRoutedFontAttrDetail;
	};

public:
	RSettings();
	void LoadSettingsL(RFs & aFs, const TDesC & aDirPath, const TDesC & aFileName);
	inline TBool IsEnabled() const { return iEnabled; }
	inline TBool IsOneOff() const { return iOneOff; }
	inline TBool IsNativeFontToBeAdapted() const { return iAdaptNativeFont; }
	inline TInt GetLogLevel() const { return iLogLevel; }
	inline TInt GetBitmapTypeAtt() const { return iBitmapTypeAtt; }
	inline TInt GetFontTestFlag() const { return iFontTest; }
	TBool IsFontFileToBeDisabled(const TDesC & aFontFile) const;
	TInt SearchAllExtraFontFilesL(RFs & aFs);
	TInt GetNextExtraFontFileL(HBufC & aFontFile);
	TInt QueryFontMap(const TOpenFontSpec & aDesiredOpenFontSpec,
		const TFontMapEntry *& aPrimaryFontMapEntry) const;
	inline const HBufC * GetAutoFontName() const;
	inline void SetAutoFontNameL(const TDesC & aFontName);
	void ApplyFontAttr(const TFontMapEntry * aFontMapEntry,
		TOpenFontSpec & aOpenFontSpec) const;
	inline TInt GetFixFontMetricsMode() const { return iFixFontMetricsMode; }
	inline TInt GetFixCharMetricsMode() const { return iFixCharMetricsMode; }
	inline TInt GetChroma() const { return iChroma; }

private:
	enum TGroup
	{
		ENullGroup,
		EGlobal,
		EFontMap
	};
	// Implementation of MIniParser
	TInt ParseLineSection(const TDesC & aSectionName);
	TInt ParseLineNormalL(TInt aSection, TInt aLineNo, const TDesC & aKey, const TDesC & aValue);
	// Parse font string
	void ParseFontString(const TDesC & aFontString, TPtrC & aFontName, TInt & aFontHeight,
		TInt& aFontAttr, RSettings::TRoutedFontAtt * aRoutedFontAttDetail = NULL);
	// Put font name into descriptor pool.
	void PoolFontNameL(TPtrC& aFontName);
	// Apply bitmap type as global configuration (iBitmapTypeAtt).
	void ApplyGlobalBitmapType(TOpenFontSpec & aOpenFontSpec) const;

private:
	TBool iEnabled;
	TBool iOneOff;							// Create defensive blocking file automatically
	TBool iAdaptNativeFont;					// Adapt native font to open font
	TInt iLogLevel;
	TInt iBitmapTypeAtt;					// TBitmapTypeAtt
	TInt iFixFontMetricsMode;				// TFixFontMetricsMode
	TInt iFixCharMetricsMode;				// TFixCharMetricsMode

	TInt iZoomRatio;						// Global zoom ratio by percentage (Default: 100)
	TInt iZoomMinSize;						// Min. size to apply zoom.
	TInt iZoomMaxSize;						// Max. size to apply zoom.
	TInt iChroma;							// Chroma of stroke by percentage, only available for anti-aliased glyph.

	TBool iFontHeightExp;					// TODO:
	TInt iFontTest;							// Font test flag
	HBufC * iAutoFontName;					// Font name by Auto Font Picker
	CDesCArrayFlat iDisabledFontFile;		// Font files to be disabled.
	CDesCArrayFlat iExtraFontPath;			// Path (with wildcard) of extra font files to be loaded.
	CDesCArrayFlat iExtraFontFile;			// Extra font files (parsed from iExtraFontPath).
	CDesCArrayFlat iGlobalSubstFont;		// Global subst fonts to be applied to all fonts.
	CDesCArrayFlat iFontNamePool; 			// Pool for all font names used by font map.
	CArrayFixSeg<TFontMapEntry> iFontMap;	// Font map.
};



//////////////////////////////////////////////////////////////////////////
class CRouterFontRasterizer;
class MProxyMaster
{
public:
	virtual TBool IsProxyEnabled() const = 0;
	virtual CRouterFontRasterizer & Rasterizer() = 0;
};



//////////////////////////////////////////////////////////////////////////
// Use a directly extended class instead of deriving because some members of COpenFontRasterizerContext are private.
class CRouterFontRasterizerContext : public CBase
{
// ========== [START] COpenFontRasterizerContext ==========
public:
	inline void StartGlyph(TOpenFontGlyphData* aGlyphData);
	inline void WriteGlyphBit(TInt aBit);
	inline void WriteGlyphByte(TInt aByte);	// Added on Symbian OS 7
	inline void EndGlyph();

private:
	TOpenFontGlyphData * iGlyphData;
	TUint8 * iGlyphDataStart;
	TUint8 * iGlyphDataPtr;
	TUint8 * iGlyphDataEnd;
	TInt iGlyphBit;
	TInt iBytesNeeded;
	TBool iOverflow;
	TAny * iReserved; // unused; for future expansion
// ========== [END] COpenFontRasterizerContext ==========

public:
	// Simple redirective constructor.
	inline CRouterFontRasterizerContext() : iGlyphData(NULL), iGlyphStreamPtr(NULL) {}
	// Constructor for glyph stream reading.
	inline void OpenGlyphReadStream(const TUint8 * aGlyphStreamPtr);
	inline TInt CloseGlyphReadStream();
	inline void ShiftBits(TInt aNumBits);
	inline TInt ReadGlyphBit();
	inline TInt ReadGlyphBits(TInt aNumBits);		// Starting with the least significant bit
	void ConvertGlyphEncoding(const TUint8 * aSourceGlyphStreamPtr,
		const TOpenFontCharMetrics & aSourceGlyphMetrics, TOpenFontGlyphData & aDestGlyphData);
	TInt GetBytesNeeded(TInt aCode, const TUint8 * aGlyph, const TOpenFontCharMetrics & aMetrics);
	void ApplyAlgorithmicBold(TOpenFontGlyphData & aGlyphData, TInt aCode);
private:
	TInt iReadBitMask;
	const TUint8 * iGlyphStreamStart;
	const TUint8 * iGlyphStreamPtr;
};



//////////////////////////////////////////////////////////////////////////
class CProxyFontFile;
class CProxyFont;
class CRouterFontRasterizer : public COpenFontRasterizer, public MProxyMaster
{
	friend class CNativeFont;
public:
	CRouterFontRasterizer();
	~CRouterFontRasterizer();
#ifndef __SYMBIAN_9__
	IMPORT_C			// This static method is the only exported function in pre-Symbian 9.
#endif
	static COpenFontRasterizer * NewL();
	void DecorateFontFilesL();
	//void ReorderFontFiles();
	COpenFontFile * NewFontFileL(TInt aUid, const TDesC& aFileName, RFs& aFs);
	//void AddFontToFontStore(CFont & aFont);
	//void RemoveFontFromFontStore(CFont & aFont);
	//inline TInt GetNearestNativeFontInPixelsL(CBitmapFont *& aFont, const TOpenFontSpec & aFontSpec, TInt aMaxHeight);
	//TInt GetNearestBitmapFontInPixels(CBitmapFont *& aFont, const TFontSpec & aFontSpec);
	inline RHeap * SharedHeap() { return iSharedHeap; }
	TBool GetNearestOpenFontFileByName(const TOpenFontSpec & aDesiredFontSpec,
		COpenFontFile *& aOpenFontFile, TInt aMaxHeight);
	TInt VerticalTwipsToPixels(TInt aTwipsHeight) const;
	// Add font file directly (bypass proxy)
	TUid AddNativeFontFileL(const TDesC & aFileName);
	// Start-up protection
	inline void Disable() { iProtectionFlag = EDisabled; }
	inline TBool IsDisabled() { return (EDisabled == iProtectionFlag); }
	inline TBool IsSafeMode() { return (ESafeMode == iProtectionFlag); }
	// Proxy control
	inline void EnableProxy() { iProxyEnabled = ETrue; }
	inline void DisableProxy() { iProxyEnabled = EFalse; }
	// Settings
	inline RSettings & Settings() { return iSettings; }
	// Save type-id of CProxyFont
	inline void SaveProxyFontTypeId(TInt aTypeId)
		{ if (NULL == iProxyFontTypeId) iProxyFontTypeId = aTypeId; }
	inline TInt ProxyFontTypeId() { return iProxyFontTypeId; }
	// Counter of proxy font
	inline void IncProxyFontCounter()
		{ iProxyFontCount ++; /*dbgprint(level_debug, "Inc: %d", iProxyFontCount);*/ }
	inline void DecProxyFontCounter()
		{ iProxyFontCount --; /*dbgprint(level_debug, "Dec: %d", iProxyFontCount);*/ }
	inline RFs* FileSession() { return iFileSession; }
	inline RDebugHelper & Debug() { return iDebugHelper; }

private:
	TBool ConstructL(RFs & aFs);
	CProxyFontFile * ProxyNewFontFileL(TInt aUid, const TDesC & aFileName, RFs & aFs);
	TBool CheckNokiaBug(COpenFontFile * aOpenFile, CFontStore * aFontStore);
	// Derived from class MProxyMaster
	TBool IsProxyEnabled() const { return iProxyEnabled; }
	CRouterFontRasterizer & Rasterizer() { return * this; }
	// Startup protection which automatically protect system against halt in startup phase.
	TBool StartupProtectionL(const TDesC& aFileName, RFs& aFs);


private:
	CRouterFontRasterizerContext * iContext;    // Context for glyph stream process.
	RFs* iFileSession;					// File session handle, owned by FBS
	RHeap* iSharedHeap;					// Shared heap, owned by FBS
	// Startup Protection
	enum TFontRouterState
	{
		EInitializing = 0,				// Initializing (only for first entry of protection function)
		EDisabled = 1,					// Protective disabled
		ESafeMode = 2,					// Safe mode
		EInitialized = 3				// Initialized (notice: this value is in the range of TickCount)
	};
	TUint iProtectionFlag;				// Flag for startup protection. TFontRouterState or TickCount since loading if above 2.
	//CRouterFontFile * iRouterFontFile;	// CRouterFontFile object, to avoid duplicate loading and reorder font files.
	TBool iProxyEnabled;
	RSettings iSettings;				// Settings
	CFontStore * iFontStore;			// System font store, always non-NULL if FontRouter is enabled.
	CProxyFont * iGlobalSubstFont;		// Global subst font used by all the other proxy fonts.
	TInt iProxyFontTypeId;				// Type-ID of CProxyFont
	TInt iProxyFontCount;				// Counter of proxy font

	TOpenFontSpec * iFontSpec;			// For font spec marking among CProxyFontFiles. (assuming that the address of font spec is not changed)
	TFontSpecMark iFontSpecMark;

	RDebugHelper iDebugHelper;			// Debug helper
};


class CProxyFontRasterizer : public COpenFontRasterizer
{
public:
	CProxyFontRasterizer(COpenFontRasterizer & aProxiedRasterizer, MProxyMaster & aProxyMaster)
		: iProxiedRasterizer(& aProxiedRasterizer), iMaster(aProxyMaster) {}
	~CProxyFontRasterizer();
	COpenFontFile * NewFontFileL(TInt aUid, const TDesC & aFileName, RFs & aFileSession);
private:
	COpenFontRasterizer * iProxiedRasterizer;
	MProxyMaster & iMaster;
};


class CProxyFontFile : public COpenFontFile
{
public:
	// This factory method protect CProxyFontFile against nested proxing.
	static CProxyFontFile * NewL(COpenFontFile & aProxiedFontFile, MProxyMaster & aMaster);
	~CProxyFontFile();
	TBool HasUnicodeCharacterL(TInt aFaceIndex, TInt aCode) const;
	void GetNearestFontInPixelsL(
		RHeap* aHeap, COpenFontSessionCacheList* aSessionCacheList,
		const TOpenFontSpec& aDesiredFontSpec, TInt aPixelWidth,TInt aPixelHeight,
		COpenFont*& aFont,TOpenFontSpec& aActualFontSpec);
#ifdef __SYMBIAN_9__
	void GetNearestFontToDesignHeightInPixelsL(
		RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
		const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
		COpenFont *& aFont, TOpenFontSpec & aActualFontSpec);
	void GetNearestFontToMaxHeightInPixelsL(
		RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
		const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
		COpenFont *& aFont, TOpenFontSpec & aActualFontSpec, TInt aMaxHeight);
#endif
private:
	CProxyFontFile(COpenFontFile & aProxiedFontFile, MProxyMaster & aMaster);
	inline TBool IsProxyEnabled(void) const
		{ return ((NULL == iProxiedFontFile) || iMaster.IsProxyEnabled()); }
	inline void UpdateFaceAttribListL();
	static void SetFontSpecMark(TOpenFontSpec & aDesiredFontSpec, TFontSpecMark aMark);
	static TFontSpecMark GetFontSpecMark(const TOpenFontSpec & aDesiredFontSpec);
	inline TBool Preprocess(RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
		const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
		COpenFont *& aFont, TOpenFontSpec & aActualFontSpec, TInt aMaxHeight);
	TBool DoPreprocess(RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
		const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
		COpenFont *& aFont, TOpenFontSpec & aActualFontSpec, TInt aMaxHeight);
	COpenFontFile * iProxiedFontFile;
	MProxyMaster & iMaster;
	TOpenFontSpec iTempFontSpec;		// To replace the usage of temperarily allocated one in DoPreprocess().
};


class CProxyFont : public COpenFont
{
public:
	static CProxyFont * NewL(RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
		COpenFontFile * aFile, TInt aFaceIndex, COpenFont & aOpenFont,
		MProxyMaster & aMaster, COpenFont * aSubstFont = NULL);
	~CProxyFont();
	//inline void operator delete(TAny*, TAny*) {}		// This leads to panic in User::Free(this)
	void RasterizeL(TInt aCode, TOpenFontGlyphData * aGlyphData);
	inline void SetDesiredFontSpec(const TOpenFontSpec & aDesiredFontSpec)
		{ iDesiredFontSpec = aDesiredFontSpec; }
	inline void SetYAdjust(TInt aYAdjust) { iYAdjust = aYAdjust; }
	inline void SetCharGapAdjust(TInt aCharGapAdjust) { iCharGapAdjust = aCharGapAdjust; }
	inline void SetChromaAdjust(TUint aChromaAdjust) { iChromaAdjust = aChromaAdjust; }
#ifdef __SYMBIAN_9__
	inline void SetLineGap(TInt aLineGap) { iFontLineGap = aLineGap; }
#endif
private:
	CProxyFont(RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
		COpenFontFile * aFile, TInt aFaceIndex, COpenFont & aOpenFont,
		MProxyMaster & aMaster, COpenFont * aSubstFont);
	void UpdateMetrics();
	void LogFontMetrics() const;
	void FixFontMetricsL();
	inline static TBool GlyphCrc(TUint16 & aCrc, const TOpenFontGlyphData & aGlyphData);
	inline RDebugHelper & Debug() { return iMaster.Rasterizer().Debug(); }

	TOpenFontSpec iDesiredFontSpec;
	COpenFont * iProxiedFont;
	COpenFont * iSubstFont;
	MProxyMaster & iMaster;
	TInt iReplCharGlyphSize;
	TUint16 iReplCharGlyphCrc;
	TInt16 iReserved;		// Padding
	TInt iYAdjust;
	TInt iCharGapAdjust;
	TUint iChromaAdjust;
};



class CNativeFontFile : public COpenFontFile
{
public:
	// Constructor/Destructor
	static CNativeFontFile * NewL(CRouterFontRasterizer & aRasterizer, TInt aUid,
		const TDesC& aFileName, const RFs & aFs);
	~CNativeFontFile();
	TInt VerticalTwipsToPixels(TInt aTwipsHeight) const;

	// Normal virtual functions
	TBool HasUnicodeCharacterL(TInt aFaceIndex, TInt aCode) const;
	void GetNearestFontInPixelsL(RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList, const TOpenFontSpec & aDesiredFontSpec,
									TInt aPixelWidth, TInt aPixelHeight, COpenFont *& aRetFont, TOpenFontSpec & aActualFontSpec);
#ifdef __SYMBIAN_9__
	void GetNearestFontToDesignHeightInPixelsL(
		RHeap* /*aHeap*/, COpenFontSessionCacheList* /*aSessionCacheList*/,
		const TOpenFontSpec& /*aDesiredFontSpec*/, TInt /*aPixelWidth*/, TInt /*aPixelHeight*/,
		COpenFont*& /*aFont*/, TOpenFontSpec& /*aActualFontSpec*/);
	void GetNearestFontToMaxHeightInPixelsL(
		RHeap* /*aHeap*/, COpenFontSessionCacheList* /*aSessionCacheList*/,
		const TOpenFontSpec& /*aDesiredFontSpec*/, TInt /*aPixelWidth*/, TInt /*aPixelHeight*/,
		COpenFont*& /*aFont*/, TOpenFontSpec& /*aActualFontSpec*/, TInt /*aMaxHeight*/);
#endif
	inline void ReleaseFont(CFont * aFont) { iFontStore->ReleaseFont(aFont); }

private:
	CNativeFontFile(CRouterFontRasterizer & aRasterizer, TInt aUid,
		const TDesC & aFileName, CFontStore & aFontStore);

private:
	TInt GetFaceIndex(const CFont & aFont) const;

	CRouterFontRasterizer & iRasterizer;
	CFontStore * iFontStore;
};


class CNativeFont : public COpenFont
{
public:
	static CNativeFont * NewL(CRouterFontRasterizer & aRasterizer,
		COpenFontSessionCacheList * aSessionCacheList, COpenFontFile * aFile, TInt aFaceIndex, CFont & aFont);
	~CNativeFont();
	//void operator delete(TAny * aProxyFont, TAny * aPlace) {}
	void RasterizeL(TInt aCode, TOpenFontGlyphData * aGlyphData);
	inline void SetAlgorithmicBold(TBool aBold) { iAlgorithmicBold = aBold; }

private:
	CNativeFont(CRouterFontRasterizer & aRasterizer, COpenFontSessionCacheList * aSessionCacheList,
		COpenFontFile * aFile, TInt aFaceIndex, CFont & aFont);
	CNativeFontFile * File() { return static_cast<CNativeFontFile *>(COpenFont::File()); }

	CRouterFontRasterizer & iRasterizer;
	CBitmapFont * iBitmapFont;
	TUint8 * iReplCharGlyph;
	TBool iAlgorithmicBold;
};



class TOpenFontFaceAttribEx : public TOpenFontFaceAttribBase
{
public:
	enum	// for iReserved
	{
		EBitmapType = 1
	};
	// Coverage[4] (only for private indication)
	enum
	{
		ENumberSet = 1	// Numbers
	};
	inline static const TOpenFontFaceAttribEx & Cast(const TOpenFontFaceAttribBase & aBase)
		{ return reinterpret_cast<const TOpenFontFaceAttribEx &>(aBase); }
	inline static TOpenFontFaceAttribEx & Cast(TOpenFontFaceAttribBase & aBase)
		{ return reinterpret_cast<TOpenFontFaceAttribEx &>(aBase); }
	inline void AddCoverage(TInt aIndex, TUint aCoverage) { iCoverage[aIndex] |= aCoverage; }
	inline TBool CheckCoverage(TInt aIndex, TUint aCoverage) const { return iCoverage[aIndex] & aCoverage; }
	inline void SetReserved(TInt32 aValue) { iReserved = aValue; }
	inline TInt32 GetReserved() const { return iReserved; }
	inline void SetStyle(TInt aStyle) { iStyle = (iStyle & 0xFFFF0000) | (aStyle & 0xFFFF); }
	inline TInt Style() const { return (iStyle & 0x0000FFFF); }
};



//////////////////////////////////////////////////////////////////////////
#ifdef __SYMBIAN_9__
// Simulate THeapWalk on Symbian 9
class THeapWalk
{
protected:
	enum TCellType
	{
		EGoodAllocatedCell, EGoodFreeCell,
		EBadAllocatedCellSize, EBadAllocatedCellAddress,
		EBadFreeCellAddress, EBadFreeCellSize
	};
public:
	THeapWalk(RHeap &aHeap) : iHeap(&aHeap), iValue(0) {}
	TInt Walk()
		{ iHeap->DebugFunction(RHeap::EWalk, (TAny *) &DoInfo, this); return iValue; }
	virtual void Info(TCellType aType,TAny* aBase,TInt aLength)=0;
protected:
	inline TInt Value() const { return(iValue); }
	inline void SetValue(TInt aValue) { iValue = aValue; }
private:
	static void DoInfo(TAny* aPtr, TCellType aType, TAny* aBase, TInt aLength)
		{ ((THeapWalk *) aPtr)->Info(aType, aBase, aLength); }
	RHeap* iHeap;
	TInt iValue;
};
#endif


inline TInt _typeid(CBase & aObject)
{
	return (* reinterpret_cast<TInt *>(& aObject));
}


class THeapSearch : public THeapWalk
{
public :
	THeapSearch(RHeap& aHeap);
	TInt Search(CBase & aRefObject);
	inline CBase * FirstResult() { return iObjectFound; }
	// Override this function to deal with each object found.
	virtual void DealL(CBase * /*aObject*/) {}
private:
	void Info(TCellType aType,TAny *aBase,TInt aLength);
	RHeap & iHeap;				// The heap to search object in.
	CBase * iObjectFound;		// Object found (NULL if not found)
	CBase * iRefObject;			// Reference object
	TInt iCellLengthExpected;	// Expected length of cell
	TInt iCellHeaderLength;		// Length of cell header in current build
	TInt iTypeId;				// Address of virtual function table
};


class TOpenFontFileData
{
	CFontStore * iFontStore;
};


class TKern
{
public:
	TKern * iNext;
	TInt iLeftChar;
	TInt iRightChar;
	TInt iKernAmount;
};


class TLigature
{
public:
	TLigature * iNext;
	TPtrC iContext;
	TInt iPos;
	TInt iLength;
	TInt iResult;
};


//class TDiacritic
//{
//public:
//	TDiacritic * iNext;
//	TPtrC iContext;
//	TInt iPos;
//	TPoint iOffset;
//	TOpenFontAttachment iFromAttachment;
//	TOpenFontAttachment iToAttachment;
//};


class COpenFontGlyph
{
	TInt iCode;
	TInt iGlyphIndex;
	TOpenFontCharMetrics iMetrics;
	TUint8 * iBitmap;
};


class COpenFontGlyphTreeEntry;
class COpenFontGlyphTreeEntry : public COpenFontGlyph
{
public:
	static COpenFontGlyphTreeEntry * New(RHeap * aHeap, TInt aCode, TInt aGlyphIndex, TOpenFontCharMetrics aMetrics, const TDesC8 & aBitmap);
	COpenFontGlyphTreeEntry * iLeft;
	COpenFontGlyphTreeEntry * iRight;
	COpenFontGlyphTreeEntry * iNext;
};


class TShapeHeader
{
	TInt iGlyphCount;
	TInt iCharacterCount;
	TInt iReserved0;
	TInt iReserved1;
	char iBuffer[1];
};


class THandleCount
{
	TInt iSessionHandle;
	TInt iRefCount;
	THandleCount * iNext;
};


class COpenFontShaperCacheEntry : public CBase
{
	COpenFontShaperCacheEntry * iPrevious;
	COpenFontShaperCacheEntry * iNext;
	TShapeHeader* iShapeHeader;
	unsigned short * iText;
	TInt iStart;
	TInt iEnd;
	RHeap * iHeap;
	THandleCount * iHandleRefList;
	TInt iHandleRefCount;
};


class COpenFontGlyphCache
{
	COpenFontGlyphTreeEntry * iGlyphTree;
	TInt iGlyphCacheMemory;
	COpenFontGlyphTreeEntry * iGlyphList;
#ifdef __SYMBIAN_92__
	COpenFontShaperCacheEntry * iShaperCacheSentinel;
	TInt iShapingInfoCacheMemory;
	TInt iNumberOfShaperCacheEntries;
#endif
};


class COpenFontSessionCacheEntry : public COpenFontGlyph
{
	COpenFont * iFont;
	TInt iLastAccess;
};


class COpenFontSessionCache
{
	TInt iSessionHandle;
	COpenFontSessionCacheEntry ** iEntry;
	TInt iEntries;
	TInt iLastAccess;
};


class COpenFontSessionCacheListItem
{
	COpenFontSessionCacheListItem * iNext;
	COpenFontSessionCache * iCache;
};


class COpenFontSessionCacheList
{
	COpenFontSessionCacheListItem * iStart;
};


class CDirectFileStore;
class CFontStoreFile : public CBase
{
public:
	inline const TDesC16 & FullName() { return * (TDesC16 *)((TUint8 *) this + 0x20); }
private:
	TUid iCollectionUid;
	TInt iKPixelAspectRatio;
	TInt iUsageCount;
	CDirectFileStore * iFileStore;
	TInt iFileAddress;	// TAny * ?
	TStreamId iDataStreamId;
	TInt iFontVersion;
};


class TCharacterMetricsTable
{
public:
	RHeap * iHeap;
	TStreamId iMetricsStartId;
	TInt iCharacterMetricsStartPtr;	// TAny * ?
	TInt iNumberOfMetrics;
};


class CFontBitmap : public CBase
{
public:
	inline CFontStoreFile * FontStoreFile()
		{ return (CFontStoreFile *)((TUint8 *) this + iFontStoreFileOffset); }
	RHeap * iHeap;
	TInt iFontStoreFileOffset;
	TUid iUid;
	TInt8 iPosture;
	TInt8 iStrokeWeight;
	TInt8 iIsProportional;
	TBool iIsInRAM;
	TInt iUsageCount;
	TInt8 iCellHeightInPixels;
	TInt8 iAscentInPixels;
	TInt8 iMaxCharWidthInPixels;
	TInt8 iMaxNormalCharWidthInPixels;
	TInt iBitmapEncoding;
	TInt iNumCodeSections;
	TInt iCodeSectionListOffset;
	TCharacterMetricsTable iCharacterMetricsTable;
	TBool iComponentsRestored;	// TInt ?
	TInt iAllocMemCounter_Offsets;
	TInt iAllocMemCounter_Bitmaps;
	TInt iFontCapitalAscent;
	TInt iFontMaxAscent;
	TInt iFontStandardDescent;
	TInt iFontMaxDescent;
	TInt iFontLineGap;
};


class TTypefaceFontBitmap
{
public:
	TInt HeightInPixels()
	{
		return iHeightFactor * iFontBitmap->iCellHeightInPixels;
	}
	TTypeface * iTypeface;
	CFontBitmap * iFontBitmap;
	TInt8 iHeightFactor;
	TInt8 iWidthFactor;
};



#endif // __FONTROUTER_INCLUDED__
