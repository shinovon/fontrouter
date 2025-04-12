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

#include <e32std.h>
#include <fntstore.h>
#include <hal.h>		// class HAL

#include "FontRouter.hpp"
#include "DebugHelper.hpp"

#ifdef __SYMBIAN_9__
#include <ImplementationProxy.h>
#endif

#ifdef __SI__
#define _LIT(a,b)
#endif

_LIT(KFontRouterVer,			"Starting FontRouter LT 2.08 Build 20071109...");
const TUint16 KMetricsProbeCharacters[] =
{
	0x7ABF,		// Chinese charecter "��"
	'A',
	'g'
};


_LIT(KReservedFontFile1,		"Z:\\System\\Fonts\\CEurope.gdr");
_LIT(KReservedFontFile2,		"Z:\\System\\Fonts\\CalcEur.gdr");
_LIT(KIniPath, 					"Y:\\Data\\Fonts\\");

// [Startup Guard tag files]
// Disable FontRouter directly. (available in all drives)
_LIT(KSGFileDisable,			"Disable.frt");
_LIT(KSGFileDisableWithPath,	"C:\\Disable.frt");


#if (!defined EKA2)
GLDEF_C TInt E32Dll(TDllReason /*aReason*/)
{
	return KErrNone;
}
#endif


inline TBool IsFileInDriveZ(const TDesC & aFileName)
{
	return (aFileName.Length() > 1 && (aFileName[0] == 'Z' || aFileName[0] == 'z') && aFileName[1] == ':');
}


CProxyFontRasterizer::~CProxyFontRasterizer()
{
	// Delete proxied font rasterizer.
	delete iProxiedRasterizer;
}


COpenFontFile * CProxyFontRasterizer::NewFontFileL(TInt aUid, const TDesC & aFileName, RFs & aFs)
{
	// DisableFontFile can also be implemented here.
	COpenFontFile * file = iProxiedRasterizer->NewFontFileL(aUid, aFileName, aFs);
	if (NULL == file)
		return NULL;

	return CProxyFontFile::NewL(* file, iMaster);
}


CProxyFontFile * CProxyFontFile::NewL(COpenFontFile & aProxiedFontFile, MProxyMaster & aMaster)
{
	CProxyFontFile * proxy_font_file = new(ELeave) CProxyFontFile(aProxiedFontFile, aMaster);

	// Protect newly created CProxyFontFile against nested proxying.
	if (_typeid(* proxy_font_file) == _typeid(aProxiedFontFile))
	{
		proxy_font_file->iProxiedFontFile = NULL; // Detach the proxied font file before deletion.
		delete proxy_font_file;
		//lint --e(826)
		return (reinterpret_cast<CProxyFontFile *>(& aProxiedFontFile)); // No proxying applied.
	}

	// Duplicate the face attrib list.
	CleanupStack::PushL(proxy_font_file);
	{
		proxy_font_file->UpdateFaceAttribListL();
	}
	CleanupStack::Pop(proxy_font_file);

	return proxy_font_file;
}


CProxyFontFile::CProxyFontFile(COpenFontFile & aProxiedFontFile, MProxyMaster & aMaster)
	: COpenFontFile(aProxiedFontFile.Uid().iUid, aProxiedFontFile.FileName())
	, iProxiedFontFile(& aProxiedFontFile), iMaster(aMaster)
{
}


CProxyFontFile::~CProxyFontFile()
{
	// Delete proxied font file first.
	delete iProxiedFontFile;
}


inline void CProxyFontFile::UpdateFaceAttribListL()
{
	// Check difference
	if (FaceCount() == iProxiedFontFile->FaceCount())
		return;

	// Clear face attrib list first.
	iFaceAttrib.Reset();

	// Duplicate face attribs from proxied font file.
	for (TInt i = 0; i < iProxiedFontFile->FaceCount(); i ++)
	{
		const TOpenFontFaceAttrib & face_attrib = iProxiedFontFile->FaceAttrib(i);

		// Log open font
		TBuf<4> styleText;
		if (face_attrib.IsSymbol()) styleText.Append('#');
		if (face_attrib.IsBold()) styleText.Append('B');
		if (face_attrib.IsItalic()) styleText.Append('I');
		if (face_attrib.IsSerif()) styleText.Append('S');
		if (face_attrib.IsMonoWidth()) styleText.Append('M');

		TPtrC name(face_attrib.Name());
		__dbgprint(level_info, "Font:\t\t%S [%d~ S<%S>, C<%08x%08x%08x%08x>]",
			& name, face_attrib.MinSizeInPixels(), & styleText, face_attrib.Coverage()[0],
			face_attrib.Coverage()[1], face_attrib.Coverage()[2], face_attrib.Coverage()[3]);
		if (face_attrib.FamilyName() != face_attrib.Name())
		{
			name.Set(face_attrib.FamilyName());
			__dbgprint(level_info, "  Family name:\t%S", & name);
		}
		if (face_attrib.LocalFullName() != face_attrib.Name())
		{
			name.Set(face_attrib.LocalFullName());
			__dbgprint(level_info, "  Local name:\t%S", & name);
		}
		if (face_attrib.LocalFamilyName() != face_attrib.Name())
		{
			name.Set(face_attrib.LocalFamilyName());
			__dbgprint(level_info, "  Local family:\t%S", & name);
		}

		AddFaceL(face_attrib);
	}
}


TBool CProxyFontFile::HasUnicodeCharacterL(TInt aFaceIndex, TInt aCode) const
{
	//if (EFalse == IsProxyEnabled())
	//	return EFalse;

	return iProxiedFontFile->HasUnicodeCharacterL(aFaceIndex, aCode);

	// No need to call UpdateFaceAttribListL because this is a const function.
}


// Return value: Whether to continue process as normal.
inline TBool CProxyFontFile::Preprocess(
	RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
	const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
	COpenFont *& aFont, TOpenFontSpec & aActualFontSpec, TInt aMaxHeight)
{
	TFontSpecMark mark = GetFontSpecMark(aDesiredFontSpec);
	if (EFontSpecByPass == mark)
		return EFalse;
	else if (EFontSpecNormal == mark)
		return ETrue;

	// The line must be placed before calling DoPreprocess(), as it stops the recursive call here.
	SetFontSpecMark(const_cast<TOpenFontSpec &>(aDesiredFontSpec), EFontSpecNormal);

	if (DoPreprocess(aHeap, aSessionCacheList, aDesiredFontSpec, aPixelWidth,
		aPixelHeight, aFont, aActualFontSpec, aMaxHeight))
	{
		SetFontSpecMark(const_cast<TOpenFontSpec &>(aDesiredFontSpec), EFontSpecByPass);
		return EFalse;
	}
	else
		return ETrue;
}


// Return value: Whether font request is handled successfully in preprocess stage.
//lint -e{715}	"'aMaxHeight' not referenced"
TBool CProxyFontFile::DoPreprocess(
	RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
	const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
	COpenFont *& aFont, TOpenFontSpec & aActualFontSpec, TInt aMaxHeight)
{
	// Set aFont to NULL for later direct return on failure.
	aFont = NULL;
	CRouterFontRasterizer & rasterizer = iMaster.Rasterizer();

	//rasterizer.EnableProxy();	// Enable proxies by default.

	// Check "Disabled" flag.
	if (rasterizer.IsDisabled())
		return EFalse;

	// Read settings for font map info.
	const RSettings & settings = rasterizer.Settings();
	const RSettings::TFontMapEntry * font_map = NULL;
	if (KErrNone != settings.QueryFontMap(aDesiredFontSpec, font_map))
		font_map = NULL;	// For safety.

	// Whether to be ignored by FontRouter.
	if (font_map && 0 == font_map->iRoutedFontName.Length())
		return EFalse;

	iTempFontSpec = aDesiredFontSpec;

	COpenFontFile * font_file = NULL;

	if (font_map && font_map->IsUltimateWild()
		&& rasterizer.GetNearestOpenFontFileByName(aDesiredFontSpec, font_file, aMaxHeight)
		&& font_file && EFalse == IsFileInDriveZ(font_file->FileName()))
	{
		// No font map but ultimate wild entry, and font itself is belonged to user font file.
		settings.ApplyFontAttr(NULL, iTempFontSpec);
	}
	else
	{
		settings.ApplyFontAttr(font_map, iTempFontSpec);
	}

	// Try to get the right font file in our own way. (font name match at present)
	if (EFalse == rasterizer.GetNearestOpenFontFileByName(iTempFontSpec, font_file, aMaxHeight)
		|| NULL == font_file)
		return EFalse;

	if (NULL == font_file)
		return EFalse;

	// Use the right font file to get the open font we need.
	TInt result;
	TPtrC font_name = iTempFontSpec.Name();
	TPtrC file_name = font_file->FileName();
#ifdef __SYMBIAN_9__
	TRAP(result, font_file->GetNearestFontToMaxHeightInPixelsL(aHeap, aSessionCacheList, iTempFontSpec,
		aPixelWidth, aPixelHeight, aFont, aActualFontSpec, aMaxHeight));
#else
	TRAP(result, font_file->GetNearestFontInPixelsL(aHeap, aSessionCacheList, iTempFontSpec,
		aPixelWidth, aPixelHeight, aFont, aActualFontSpec));
#endif
	if (KErrNone != result)
	{
		__dbgprint(level_error, "Error %d occurred when getting font \"%S\" @ %d from file \"%S\"!",
			result, & font_name, iTempFontSpec.Height(), & file_name);
		return EFalse;
	}

	if (NULL == aFont)
	{
		__dbgprint(level_error, "Failed to get the font \"%S \" @ %d from file \"%S\"!",
			& font_name, iTempFontSpec.Height(), & file_name);
		return EFalse;
	}

	// If we successfully get the font, stop proxies for further processes.
	//rasterizer.DisableProxy();

	if (EFalse == settings.IsNativeFontToBeAdapted())
	{
		// Force the actual font spec to be same with desired font spec (except bitmap type),
		//	 in order to gain preference.
		TGlyphBitmapType bitmap_type = aActualFontSpec.BitmapType();
		aActualFontSpec = aDesiredFontSpec;
		// Fix: Only set bitmap type when needed. EMonochromeGlyphBitmap will be considered different
		//	 from EDefaultGlyphBitmap on font scoring. So no changes will be made between them.
		if (aDesiredFontSpec.BitmapType() > EMonochromeGlyphBitmap || bitmap_type > EMonochromeGlyphBitmap)
			aActualFontSpec.SetBitmapType(bitmap_type);
	}

	if (_typeid(* aFont) == iMaster.Rasterizer().ProxyFontTypeId())
	{
		CProxyFont * proxy_font = reinterpret_cast<CProxyFont *>(aFont);

		// Set the originally requested font spec.
		proxy_font->SetDesiredFontSpec(aDesiredFontSpec);

		// Apply font attributes
		if (font_map)
		{
#ifdef __SYMBIAN_9__
			if (font_map->iRoutedFontAttr & RSettings::EFontAttLineGapAdjust)
				proxy_font->SetLineGap
					(proxy_font->FontLineGap() + font_map->iRoutedFontAttrDetail.iLineGapAdjust);
#else
			// TODO: Implementation for pre-Symbian 9
#endif
			if (font_map->iRoutedFontAttr & RSettings::EFontAttYAdjust)
				proxy_font->SetYAdjust(font_map->iRoutedFontAttrDetail.iYAdjust);
			if (font_map->iRoutedFontAttr & RSettings::EFontAttCharGapAdjust)
				proxy_font->SetCharGapAdjust(font_map->iRoutedFontAttrDetail.iCharGapAdjust);
			if (font_map->iRoutedFontAttr & RSettings::EFontAttChromaAdjust)
				proxy_font->SetChromaAdjust(font_map->iRoutedFontAttrDetail.iChromaAdjust);
		}
	}

	return ETrue;
}


TFontSpecMark CProxyFontFile::GetFontSpecMark(const TOpenFontSpec & aDesiredFontSpec)
{
//	return (EFontSpecMark
//		== (reinterpret_cast<const TOpenFontFaceAttribEx &>(aDesiredFontSpec)).GetReserved());
	return (TFontSpecMark)(TOpenFontFaceAttribEx::Cast(aDesiredFontSpec).GetReserved());
}


void CProxyFontFile::SetFontSpecMark(TOpenFontSpec & aDesiredFontSpec, TFontSpecMark aMark)
{
	TOpenFontFaceAttribEx::Cast(aDesiredFontSpec).SetReserved((TInt32) aMark);
}


void CProxyFontFile::GetNearestFontInPixelsL(
	RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
	const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
	COpenFont * & aFont, TOpenFontSpec & aActualFontSpec)
{
	if (EFalse == Preprocess(aHeap, aSessionCacheList, aDesiredFontSpec, aPixelWidth, aPixelHeight,
		aFont, aActualFontSpec, 0)) return;

	if ( ! IsProxyEnabled()) return;

	aFont = NULL;

	iProxiedFontFile->GetNearestFontInPixelsL(aHeap, aSessionCacheList, aDesiredFontSpec,
		aPixelWidth, aPixelHeight, aFont, aActualFontSpec);

	if (aFont)
	{
		CleanupStack::PushL(aFont);
		{
			aFont = CProxyFont::NewL(aHeap, aSessionCacheList, this, aFont->FaceIndex(),
				* aFont, iMaster);
		}
		CleanupStack::Pop();	// aFont now is no longer the one pushed.
		// Record type id of CProxyFont
		iMaster.Rasterizer().SaveProxyFontTypeId(_typeid(* aFont));
	}

	UpdateFaceAttribListL();
}


#ifdef __SYMBIAN_9__
void CProxyFontFile::GetNearestFontToDesignHeightInPixelsL(
	RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
	const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
	COpenFont * & aFont, TOpenFontSpec & aActualFontSpec)
{
	if (EFalse == Preprocess(aHeap, aSessionCacheList, aDesiredFontSpec, aPixelWidth, aPixelHeight,
			aFont, aActualFontSpec, 0)) return;

	if ( ! IsProxyEnabled()) return;

	aFont = NULL;
	iProxiedFontFile->GetNearestFontToDesignHeightInPixelsL(aHeap, aSessionCacheList, aDesiredFontSpec,
		aPixelWidth, aPixelHeight, aFont, aActualFontSpec);

	if (aFont)
	{
		CleanupStack::PushL(aFont);
		{
			aFont = CProxyFont::NewL(aHeap, aSessionCacheList, this, aFont->FaceIndex(),
				* aFont, iMaster);
		}
		CleanupStack::Pop();	// aFont now is no longer the one pushed.
		// Record type id of CProxyFont
		iMaster.Rasterizer().SaveProxyFontTypeId(_typeid(* aFont));
	}

	UpdateFaceAttribListL();
}


void CProxyFontFile::GetNearestFontToMaxHeightInPixelsL(
	RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
	const TOpenFontSpec & aDesiredFontSpec, TInt aPixelWidth, TInt aPixelHeight,
	COpenFont * & aFont, TOpenFontSpec & aActualFontSpec, TInt aMaxHeight)
{
	if (EFalse == Preprocess(aHeap, aSessionCacheList, aDesiredFontSpec, aPixelWidth, aPixelHeight,
			aFont, aActualFontSpec, aMaxHeight)) return;

	if ( ! IsProxyEnabled()) return;

	aFont = NULL;
	iProxiedFontFile->GetNearestFontToMaxHeightInPixelsL(aHeap, aSessionCacheList, aDesiredFontSpec,
		aPixelWidth, aPixelHeight, aFont, aActualFontSpec, aMaxHeight);

	if (aFont)
	{
		CleanupStack::PushL(aFont);
		{
			aFont = CProxyFont::NewL(aHeap, aSessionCacheList, this, aFont->FaceIndex(),
				* aFont, iMaster);
		}
		CleanupStack::Pop();	// aFont now is no longer the one pushed.
		// Record type id of CProxyFont
		iMaster.Rasterizer().SaveProxyFontTypeId(_typeid(* aFont));
	}

	UpdateFaceAttribListL();
}
#endif


CProxyFont::CProxyFont(RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
	COpenFontFile * aFile, TInt aFaceIndex, COpenFont & aOpenFont,
	MProxyMaster & aMaster, COpenFont * aSubstFont)
	: COpenFont(aHeap, aSessionCacheList, aFile, aFaceIndex),
	iProxiedFont(& aOpenFont), iSubstFont(aSubstFont), iMaster(aMaster),
	iChromaAdjust(aMaster.Rasterizer().Settings().GetChroma())	// Default value
{
	aMaster.Rasterizer().IncProxyFontCounter();
}


CProxyFont::~CProxyFont()
{
	iMaster.Rasterizer().DecProxyFontCounter();
	const TOpenFontFaceAttrib * face = FaceAttrib();
	if (face)
	{
		TPtrC face_name = face->Name();
		dbgprint(level_debug, "  Release font: %S", & face_name);
	}

	delete iSubstFont;
	iSubstFont = NULL;
	delete iProxiedFont;
	iProxiedFont = NULL;
}


CProxyFont * CProxyFont::NewL(RHeap * aHeap, COpenFontSessionCacheList * aSessionCacheList,
	COpenFontFile * aFile, TInt aFaceIndex, COpenFont & aOpenFont,
	MProxyMaster & aMaster, COpenFont * aSubstFont)
{
	CProxyFont * proxy_font = (CProxyFont*) aHeap->AllocLC(sizeof(CProxyFont));
	{
		(void) new(proxy_font) CProxyFont(aHeap, aSessionCacheList, aFile,
			aFaceIndex, aOpenFont, aMaster, aSubstFont);

		proxy_font->LogFontMetrics();
		proxy_font->FixFontMetricsL();
		proxy_font->UpdateMetrics();

		// Store CRC of replacement char glyph, only when subst font is valid.
		if (aSubstFont)
		{
			TOpenFontGlyphData * temp = TOpenFontGlyphData::New(& User::Heap(),
				proxy_font->Metrics().Size() * proxy_font->Metrics().MaxWidth() + 100);
			if (temp)
			{
				CleanupStack::PushL(temp);
				{
					// TODO: Use one more unicode to ensure it is not in this font.
					// Since iReplCharGlyphSize and iReplCharGlyphCrc is not set yet,
					//	 KUnicodeNullChar will only be rasterized in current font.
					proxy_font->RasterizeL(KUnicodeNullChar, temp);

					proxy_font->iReplCharGlyphSize = temp->BytesNeeded();
					GlyphCrc(proxy_font->iReplCharGlyphCrc, * temp);
				}
				CleanupStack::PopAndDestroy(temp);
			}
		}
	}
	CleanupStack::Pop();		// proxy_font
	return proxy_font;
}


void CProxyFont::UpdateMetrics()
{
	iMetrics = iProxiedFont->Metrics();
#ifdef __SYMBIAN_9__
	iFontCapitalAscent		= iProxiedFont->FontCapitalAscent();
	iFontMaxAscent			= iProxiedFont->FontMaxAscent();
	iFontStandardDescent	= iProxiedFont->FontStandardDescent();
	iFontMaxDescent 		= iProxiedFont->FontMaxDescent();
	iFontLineGap			= iProxiedFont->FontLineGap();
#endif
}


void CProxyFont::RasterizeL(TInt aCode, TOpenFontGlyphData * aGlyphData)
{
	// Validate argument (consider removal)
	if (NULL == aGlyphData)
		User::Leave(KErrArgument);

	// TODO: General unicode redirection can be implemented here.

	// Test unicode coverage
	TInt flag = iMaster.Rasterizer().Settings().GetFontTestFlag();
	if (flag)
	{
		COpenFontFile * file = iProxiedFont->File();
		if (file && EFalse == file->HasUnicodeCharacterL(iProxiedFont->FaceIndex(), aCode))
		{
			TPtrC font_name = file->FaceAttrib(iProxiedFont->FaceIndex()).Name();
			TPtrC desired_name = iDesiredFontSpec.Name();
			dbgprint(level_info, "Char 0x%04x not found in font %S redirected from %S @ %d!",
				aCode, & font_name, & desired_name, iDesiredFontSpec.Height());
		}
	}

	iProxiedFont->RasterizeL(aCode, aGlyphData);

	TOpenFontCharMetrics * char_metrics = const_cast<TOpenFontCharMetrics *>(aGlyphData->Metrics());
	User::LeaveIfNull(char_metrics);	// Protection

	// If subst font is present, check whether routing is needed.
	TUint16 crc;
 	if (iSubstFont
		&& aGlyphData->BytesNeeded() == iReplCharGlyphSize
		&& GlyphCrc(crc, * aGlyphData) && crc == iReplCharGlyphCrc)
	{	// aCode can not be rasterized by proxied font, use subst font instead.
		aGlyphData->SetPointersToInternalBuffers();
		aGlyphData->SetBytesNeeded(0);
		iSubstFont->RasterizeL(aCode, aGlyphData);
	}
	else	// Subst font not used
	{
		// Adjust horizontal bearing Y.
		char_metrics->SetHorizBearingY(char_metrics->HorizBearingY() + iYAdjust);
	}

	// Apply char gap adjust
	char_metrics->SetHorizAdvance(char_metrics->HorizAdvance() + iCharGapAdjust);

	// Apply chroma to glyph bitmap (not capable for monochrome bitmap type)
	TInt glyph_bytes = char_metrics->Width() * char_metrics->Height();
	if (aGlyphData->BitmapPointer() == aGlyphData->BufferStart()  // only for buffered glyph
		&& 100 != iChromaAdjust && aGlyphData->BytesNeeded() >= glyph_bytes)
	{
		TUint8 * start = aGlyphData->BufferStart();
		for (TInt i = 0; i < glyph_bytes; i ++)
		{
			TUint new_byte = * (start + i) * iChromaAdjust / 100;
			* (start + i) = (TUint8) (new_byte > 255 ? 255 : new_byte);
		}
	}

	//if (File())
	//	__dbgprint(level_info, "%S @ %d\t[%c]: H=%d, Y=%d,", & FaceAttrib()->Name(),
	//		Metrics().Size(), aCode, char_metrics->Height(), char_metrics->HorizBearingY());
}


void CProxyFont::LogFontMetrics() const
{
	const TOpenFontMetrics & metrics = iProxiedFont->Metrics();
	TPtrC font_name;
	if (FaceAttrib())
		font_name.Set(FaceAttrib()->Name());

#ifdef __SYMBIAN_9__
	__dbgprint(level_info, "%S: S=%d, A=%d, D=%d, MH=%d, MD=%d, LG=%d, CA=%d, SD=%d, MA=%d, MD=%d, Y+=%d",
		& font_name,
		metrics.Size(), metrics.Ascent(), metrics.Descent(), metrics.MaxHeight(), metrics.MaxDepth(),
		iProxiedFont->FontLineGap(), iProxiedFont->FontCapitalAscent(), iProxiedFont->FontStandardDescent(),
		iProxiedFont->FontMaxAscent(), iProxiedFont->FontMaxDescent(), iYAdjust);
#else
	__dbgprint(level_info, "%S: S=%d, A=%d, D=%d, MH=%d, MD=%d, Y+=%d", & font_name,
		metrics.Size(), metrics.Ascent(), metrics.Descent(), metrics.MaxHeight(), metrics.MaxDepth(), iYAdjust);
#endif
}


void CProxyFont::FixFontMetricsL()
{
	TBool changed = EFalse;
	TOpenFontMetrics & metrics = const_cast<TOpenFontMetrics &>(iProxiedFont->Metrics());

#ifdef __SYMBIAN_9__
	// Patch the font metrics bug of Nokia iTypeRast.
	//	 iTypeRast of Nokia S60 v3 phones will quirkily set iMetrics.iAscent & iDescent to zero!
	if (0 == metrics.Ascent())
	{
		changed = ETrue;
		metrics.SetAscent(iProxiedFont->FontCapitalAscent());
		if (0 == metrics.Descent())
			metrics.SetDescent(iProxiedFont->FontStandardDescent());
	}
	if (0 == metrics.MaxHeight())
	{
		changed = ETrue;
		metrics.SetMaxHeight(iProxiedFont->FontMaxAscent());
		if (0 == metrics.MaxDepth())
			metrics.SetMaxDepth(iProxiedFont->FontMaxDescent());
	}
	if (0 == metrics.Size())
	{
		changed = ETrue;
		metrics.SetSize(iProxiedFont->FontCapitalAscent() + iProxiedFont->FontStandardDescent());
	}
#endif

	// Fix possible worse metrics.
	if (0 == metrics.Ascent())
	{
		changed = ETrue;
		if (metrics.Descent() >= metrics.Size())
			metrics.SetDescent(0);
		metrics.SetAscent(metrics.Size() - metrics.Descent());
#ifdef __SYMBIAN_9__
		iProxiedFont->iFontCapitalAscent = metrics.Ascent();
#endif
	}
	if (0 == metrics.MaxHeight())
	{
		changed = ETrue;
		metrics.SetMaxHeight(metrics.Ascent());
#ifdef __SYMBIAN_9__
		iProxiedFont->iFontMaxAscent = metrics.MaxHeight();
#endif
	}

	// Fix font metrics according to configurated fix mode.
	TInt mode = iMaster.Rasterizer().Settings().GetFixFontMetricsMode();
	switch(mode)
	{
	case RSettings::EFixFontMetricsNoDescent:
		if (0 != metrics.Ascent())
		{
			changed = ETrue;

			iYAdjust = metrics.Size() - metrics.Ascent();

			metrics.SetAscent(metrics.Size());
			metrics.SetDescent(0);
			//metrics.SetMaxHeight(metrics.MaxHeight() + iYAdjust);
			metrics.SetMaxHeight(metrics.Ascent());
			//metrics.SetMaxDepth(metrics.MaxDepth() - iYAdjust);
			metrics.SetMaxDepth(0);
#ifdef __SYMBIAN_9__
			iProxiedFont->iFontCapitalAscent = metrics.Ascent();
			iProxiedFont->iFontStandardDescent = 0;
			iProxiedFont->iFontMaxAscent = metrics.Ascent();
			iProxiedFont->iFontMaxDescent = 0;
			/*
			iProxiedFont->iFontCapitalAscent += iYAdjust;
			iProxiedFont->iFontStandardDescent -= iYAdjust;
			if (iProxiedFont->iFontStandardDescent < 0)
				iProxiedFont->iFontStandardDescent = 0;
			iProxiedFont->iFontMaxAscent += iYAdjust;
			iProxiedFont->iFontMaxDescent -= iYAdjust;
			if (iProxiedFont->iFontMaxDescent < 0)
				iProxiedFont->iFontMaxDescent = 0;
			*/
#endif
		}
		break;

	case RSettings::EFixFontMetricsNone:
		//lint -fallthrough

	default:
		break;
	}

	// Auto set Y-adjust by probing sample character
	if (RSettings::EFixCharMetricsAuto == iMaster.Rasterizer().Settings().GetFixCharMetricsMode())
	{
		const TOpenFontFaceAttrib * face = iProxiedFont->FaceAttrib();
		TOpenFontGlyphData * glyph_data;
		if (face && NULL != (glyph_data = TOpenFontGlyphData::New(& User::Heap(),
				iProxiedFont->Metrics().Size() * iProxiedFont->Metrics().MaxWidth() + 100)))
		{
			CleanupStack::PushL(glyph_data);

			TInt old_y_adjust = iYAdjust;

			// Fix out-of-metrics glyph
			for (TInt char_index = 0; char_index < (TInt) ARRAY_DIM(KMetricsProbeCharacters); char_index ++)
			{
				TInt result;
				TRAP(result, iProxiedFont->RasterizeL(KMetricsProbeCharacters[char_index], glyph_data));

				TOpenFontCharMetrics * char_metrics
					= const_cast<TOpenFontCharMetrics *>(glyph_data->Metrics());

				if (KErrNone != result || glyph_data->BytesNeeded() <= 0
					|| NULL == char_metrics || char_metrics->Height() <= 0)
					continue;

				TInt orginal_y = char_metrics->HorizBearingY();

				char_metrics->SetHorizBearingY(char_metrics->HorizBearingY() + iYAdjust);

				TInt descent = Abs(metrics.Descent());
				if (char_metrics->Height() > metrics.Size())				// Out of font size
				{	// Fomula: A * (H + S) / (2 * S). For rounding, 0.5 is added.
					TInt size = metrics.Size();
					char_metrics->SetHorizBearingY
						((metrics.Ascent() * (char_metrics->Height() + size) + size) / (size * 2));
				}
				else if (char_metrics->HorizBearingY() > metrics.Ascent()) // Out of ascent
				{
					// Fix Y-Adjust according to current recalculated adjust.
					char_metrics->SetHorizBearingY(metrics.Ascent());
				}
				else if (char_metrics->Height() - char_metrics->HorizBearingY() > descent)
				{															// Out of descent
					// Fix Y-Adjust according to current recalculated adjust.
					char_metrics->SetHorizBearingY(char_metrics->Height() - descent);
				}

				iYAdjust = char_metrics->HorizBearingY() - orginal_y;
			}

			if (old_y_adjust != iYAdjust)
			{
				dbgprint(level_info, "Set Y-Adjust to %d.", iYAdjust);
			}
			CleanupStack::PopAndDestroy(glyph_data);
		}
	}

	// Log new font metrics
	if (changed)
		LogFontMetrics();
}


inline TBool CProxyFont::GlyphCrc(TUint16 & aCrc, const TOpenFontGlyphData & aGlyphData)
{
	const TUint8 * glyph = aGlyphData.BitmapPointer();
	TInt length = aGlyphData.BytesNeeded();
	if (0 == length)
	{
		aCrc = 0;
		return ETrue;
	}

	if (glyph && length > 0)
	{
		if (length > 4)
			length -= 4;	// Exclude the last 4 bytes as this may differ in cases.
		Mem::Crc(aCrc, glyph, length);
		return ETrue;
	}

	return EFalse;
}



//////////////////////////////////////////////////////////////////////////
// CRouterFontRasterizer
//
CRouterFontRasterizer::CRouterFontRasterizer()
	: iFileSession(NULL), iProtectionFlag(EInitializing), iProxyEnabled(ETrue), iGlobalSubstFont(NULL)
{
	// Important trick: load FLogger module here to enforce loading MMC driver, thus we can load fonts
	//	 from MMC card because MMC drivers is actually not loaded at this phase in some models of
	//	 Symbian phones.

	RDebugHelper::Log(KFontRouterVer);

	// Create a temporary font store for searching system font store.
	TInt result;
	CFontStore* ref = NULL;
	TRAP(result, (ref = CFontStore::NewL(& User::Heap())));
	if (result != KErrNone || NULL == ref)
	{	// Failed to create temp font store, we have to disable FontRouter.
		dbgprint(level_fatal, "Failed to create font store, FontRouter is disabled!");
		Disable();
		return;
	}

	THeapSearch heapSearch(User::Heap());
	result = heapSearch.Search(* ref);
	iFontStore = reinterpret_cast<CFontStore *>(heapSearch.FirstResult());
	if ( 1 < result )
	{
		dbgprint(level_debug, "Found %d font stores!", result);
		// TODO: Search "this" in each font store to determine the right one.
	}
	else if (0 >= result)
	{
		// Disable FontRouter then leave.
		iFontStore = NULL;
	}
	delete ref;

	if (NULL == iFontStore)
	{
		dbgprint(level_fatal, "System font store could not be located! (ErrCode: %d)", result);
		Disable();
		return;
	}

	iSharedHeap = iFontStore->iHeap;
}


CRouterFontRasterizer::~CRouterFontRasterizer()
{
	delete iGlobalSubstFont;
}


#ifndef __SYMBIAN_9__
EXPORT_C			// This static method is the only exported function in pre-Symbian 9.
#endif
COpenFontRasterizer * CRouterFontRasterizer::NewL()
{
    CRouterFontRasterizer * rasterizer = new(ELeave) CRouterFontRasterizer;
    CleanupStack::PushL(rasterizer);
    rasterizer->iContext = new(ELeave) CRouterFontRasterizerContext;
    CleanupStack::Pop(rasterizer);
    return rasterizer;
}


TBool CRouterFontRasterizer::ConstructL(RFs & aFs)
{
	iFileSession = & aFs;

	// Load settings from INI file.
	TInt result;
	TRAP(result, iSettings.LoadSettingsL(aFs, KIniPath, _L("FontRouter.ini")));
	if (KErrNone != result)
		{ dbgprint(level_error, "Exception in loading settings from FontRouter.ini! (Err %d)", result); }
	else	// Apply settings just loaded.
	{
		if (EFalse == iSettings.IsEnabled())
		{
			dbgprint(level_info, "FontRouter is disabled due to setting.", result);
			Disable();
			return EFalse;
		}
		if (iSettings.IsOneOff())
		{
			dbgprint(level_info, "FontRouter will be disabled in next reboot.", result);
			RFile file;
			if (KErrNone == file.Create(aFs, KSGFileDisableWithPath, EFileShareExclusive))
			{
				file.Flush();
				file.Close();
			}
		}
		iDebugHelper.SetLogLevel(iSettings.GetLogLevel());
		dbgprint(level_info, "Log level is set to %d.", iDebugHelper.LogLevel());

		TInt bt_att = iSettings.GetBitmapTypeAtt();
		if (bt_att > RSettings::ENoChange)
			iFontStore->SetDefaultBitmapType((TGlyphBitmapType) ((bt_att + 1) / 2));
	}

	// Check whether FontRouter is the first rasterizer.
	TInt count = iFontStore->iOpenFontRasterizerList.Count();
	if (count > 1 && (iFontStore->iOpenFontRasterizerList[0] != this))
	{
		dbgprint(level_debug, "FontRouter is not loaded as the first rasterizer, try to reorder.");
		for (TInt i = 1; i < count; i ++)
		{
			COpenFontRasterizer * & rasterizer = iFontStore->iOpenFontRasterizerList[i];
			if (this == rasterizer)
			{	// Exchange FontRouter and the first rasterizer.
				rasterizer = iFontStore->iOpenFontRasterizerList[0];
				iFontStore->iOpenFontRasterizerList[0] = this;
				break;
			}
		}
	}

	// Decorate all font files loaded before CRouterFontFile.
	DecorateFontFilesL();

	// Decorate all other open font rasterizers.
	//DecorateFontRasterizersL();

	return ETrue;
}


COpenFontFile* CRouterFontRasterizer::NewFontFileL(TInt aUid, const TDesC & aFileName, RFs & aFs)
{
	//dbgprint(level_debug, "Parsing font file %S.", & aFileName);

	if (IsDisabled())
		return NULL;	// Blocked by startup protection.

	// Return value of StartupProtectionL() means whether to continue.
	if (EFalse == StartupProtectionL(aFileName, aFs))
		return NULL;

	// If called for the first time, do some initial process.
	if (NULL == iFileSession)
	{
		if (EFalse == ConstructL(aFs))
			return NULL;

		// Search for all extra font files.
		if (iSettings.SearchAllExtraFontFilesL(aFs) > 0)
		{
			aUid = iFontStore->iOpenFontUid;	// Refresh aUid if new font files loaded.
		}
	}

	// Disable font files configurated to be disabled.
	if (iSettings.IsFontFileToBeDisabled(aFileName))
	{
		dbgprint(level_info, "Font file \"%S\" is disabled.", & aFileName);
		User::Leave(KErrCancel);
	}

	// Proxy the font file recognizable by other open font rasterizers.
	CProxyFontFile * proxied = NULL;
	HBufC * file_name = HBufC::NewLC(KMaxFileName);
	* file_name = aFileName;

	for (;;)	// Break the loop if font file loaded successfully or no font files to load.
	{
		TInt result = KErrNone;
		TRAP(result, proxied = ProxyNewFontFileL(aUid, * file_name, aFs));

		if (KErrNone != result) // Leaving occurred.
		{
			dbgprint(level_debug, "Failed to load font file \"%S\" !", file_name);
			// No break;
		}
		else if (proxied)	// Font file is loaded successfully.
		{
			// Update face attrib list for new proxied font file.
			//	 As proxy font file has duplicated the face attrib list, so no need to get the proxied faces.
			dbgprint(level_info, "Font file %S is loaded successfully.", file_name);

			// Choose auto font name if not set yet.
			if (NULL == iSettings.GetAutoFontName() && EFalse == IsFileInDriveZ(* file_name))
			{
				for (TInt i = 0; i < proxied->FaceCount(); i ++)
				{
					const TOpenFontFaceAttrib & face_attrib = proxied->FaceAttrib(i);
					TPtrC face_name = face_attrib.Name();

					// Skip symbol fonts and fonts without name.
					if (EFalse == face_attrib.IsSymbol() && face_name.Length() > 0)
					{
						iSettings.SetAutoFontNameL(face_name);
						dbgprint(level_info, "Default font: %S", & face_name);
						break;
					}
				}
			}
			break;
		}
		else if (file_name->Length() >= 4 && 0 == file_name->Right(4).CompareF(_L(".GDR")))
		{
			dbgprint(level_info, "Native font file %S is being loaded.", file_name);
			break;
		}

		// If it is neither native font file nor open font file recognized successfully, load extra font file.
		if (KErrNone != iSettings.GetNextExtraFontFileL(* file_name))
			break;
		// Continue the loop for reload on the extra font.
	}

	CleanupStack::PopAndDestroy(file_name);

	return proxied;
}


void CRouterFontRasterizer::DecorateFontFilesL()
{
	CArrayPtrFlat<COpenFontFile> & font_file_list = iFontStore->iOpenFontFileList;
	for (TInt i = 0; i < font_file_list.Count(); i ++)
	{
		COpenFontFile *& font_file = font_file_list[i];
		if (NULL == font_file) continue;

		font_file = CProxyFontFile::NewL(* font_file, * this);
	}
}


TBool CRouterFontRasterizer::CheckNokiaBug(COpenFontFile * aOpenFile, CFontStore * aFontStore)
{
	if (aOpenFile->FaceCount() > 0)
	{
		// Only match the first face. (that's enought)
		const TOpenFontFaceAttrib & face = aOpenFile->FaceAttrib(0);
		for (TInt file_index = 0; file_index < aFontStore->iOpenFontFileList.Count(); file_index ++)
		{
			COpenFontFile * file = aFontStore->iOpenFontFileList[file_index];
			if (file && file->FaceCount() > 0 && face == file->FaceAttrib(0))
				return ETrue;
		}
	}

	return EFalse;
}


CProxyFontFile * CRouterFontRasterizer::ProxyNewFontFileL(TInt aUid, const TDesC & aFileName, RFs & aFs)
{
	COpenFontFile * file = NULL;

	for (TInt i = 0; i < iFontStore->iOpenFontRasterizerList.Count(); i ++)
	{
		COpenFontRasterizer * & rasterizer = iFontStore->iOpenFontRasterizerList[i];
		if (this == rasterizer || NULL == rasterizer)	// Skip myself.
			continue;

		file = rasterizer->NewFontFileL(aUid, aFileName, aFs);
		if (file)	// Font file is successfully recognized.
		{
			// Warn about the bug that true type rasterizer of Nokia may not correctly load some font file for the first time.
			if (CheckNokiaBug(file, iFontStore))
			{
				dbgprint(level_warning,
					"Nokia iTypeRast typeface overlay bug detected on font file \"%S\".", & aFileName);
			}
			break;
		}
	}

	// If all open font rasterizer failed to recognize this font file, use native font adapter.
	if (NULL == file && iSettings.IsNativeFontToBeAdapted())
		file = CNativeFontFile::NewL(* this, aUid, aFileName, aFs);

	if (file)	// Font file is successfully recognized.
	{
		CleanupStack::PushL(file);
		CProxyFontFile * proxied = CProxyFontFile::NewL(* file, * this);  // Decorate this font file.
		CleanupStack::Pop(file);
		return proxied;
	}

	return NULL;
}


// Only match by name is implemented at present.
TBool CRouterFontRasterizer::GetNearestOpenFontFileByName(
	const TOpenFontSpec & aDesiredFontSpec, COpenFontFile *& aOpenFontFile, TInt aMaxHeight)
{
	TPtrC desired_font_name = aDesiredFontSpec.Name();

	// Ignore request with empty font name.
	if (desired_font_name.Length() <= 0)
		return EFalse;

	CArrayPtrFlat<COpenFontFile> & file_list = iFontStore->iOpenFontFileList;

	for (TInt file_index = 0; file_index < file_list.Count(); file_index ++)
	{
		COpenFontFile * font_file = file_list[file_index];
		if (NULL == font_file)
			continue;

		TInt face_count = font_file->FaceCount();

		if (face_count <= 0)
			continue;		// Skip font files without faces.

		// Special match by font file name (as font name).
		TParsePtrC file_name_parser(font_file->FileName());
		if (0 == desired_font_name.CompareF(file_name_parser.NameAndExt())
			|| 0 == desired_font_name.CompareF(file_name_parser.Name()))
		{					// Either name with ext. or just name is accepted.
			aOpenFontFile = font_file;
			return ETrue;
		}

		// Match by font name
		for (TInt face_index = 0; face_index < font_file->FaceCount(); face_index ++)
		{
			const TOpenFontFaceAttrib & face_attrib = font_file->FaceAttrib(face_index);

			// Check the min size of face against the acceptable max size.
			if (aMaxHeight && aMaxHeight < face_attrib.MinSizeInPixels())
				continue;

			if (0 == desired_font_name.CompareF(face_attrib.FullName())
				|| 0 == desired_font_name.CompareF(face_attrib.LocalFullName())
				|| 0 == desired_font_name.CompareF(face_attrib.ShortFullName())
				|| 0 == desired_font_name.CompareF(face_attrib.ShortLocalFullName())
				|| 0 == desired_font_name.CompareF(face_attrib.FamilyName())
				|| 0 == desired_font_name.CompareF(face_attrib.LocalFamilyName())
				|| 0 == desired_font_name.CompareF(face_attrib.ShortFamilyName())
				|| 0 == desired_font_name.CompareF(face_attrib.ShortLocalFamilyName()))
			{
				aOpenFontFile = font_file;
				return ETrue;
			}
		}
	}

	return EFalse;
}


TUid CRouterFontRasterizer::AddNativeFontFileL(const TDesC & aFileName)
{
	ASSERT_RET(iFontStore, KNullUid);

	TUid uid = KNullUid;

	TInt bak = * ((TInt *) & iFontStore->iOpenFontRasterizerList + 1);
	* ((TInt *) & iFontStore->iOpenFontRasterizerList + 1) = 0;
	if (0 != iFontStore->iOpenFontRasterizerList.Count())
	{
		dbgprint(level_fatal, "Font store not compatible!");
	}
	else
	{
		TInt result;
		TRAP(result, uid = iFontStore->AddFileL(aFileName));
		if (KErrNone != result)
			uid = KNullUid;
	}
	* ((TInt *) & iFontStore->iOpenFontRasterizerList + 1) = bak;

	// Erase the typefaces added by the native font file.
	iFontStore->iTypefaceList.Reset();
	return uid;
}


TInt CRouterFontRasterizer::VerticalTwipsToPixels(TInt aTwipsHeight) const
{
	// Calculate by rounding off but not truncation. (FAULT_002)
	TInt kPixelHeightInTwips = iFontStore->iKPixelHeightInTwips;
	if (0 == kPixelHeightInTwips)
		kPixelHeightInTwips = KDefaultKPixelHeightInTwips;

	return((aTwipsHeight * 1000 + kPixelHeightInTwips / 2) / kPixelHeightInTwips);
}


// Startup protection which protects system against halt in startup phase.
// Return value of StartupProtection() means whether to continue parent function.
TBool CRouterFontRasterizer::StartupProtectionL(const TDesC& aFileName, RFs& aFs)
{
	if (EInitializing == iProtectionFlag)
	{
		// Following code should be excuted only once after FontRouter loaded
		iProtectionFlag = EInitialized;

		// Check startup guard disabling tag file.
		TFindFile findFile(aFs);
		if (KErrNone == findFile.FindByDir(KSGFileDisable, _L("\\")))
		{
			Disable();
			dbgprint(level_warning, "FontRouter is disabled, please remove \"%S\" to enable it.", & KSGFileDisable);
			return EFalse;
		}

	}

	if (IsDisabled())
		return EFalse;

	if (IsSafeMode())	// In safe mode, only font files in rom will be loaded.
	{
		if (EFalse == IsFileInDriveZ(aFileName))
		{
			// Leaving cause the font file to be discarded.
			dbgprint(level_warning, "Font file \"%S\"is disabled in safe mode.", & aFileName);
			User::Leave(KErrCancel);
		}
		return EFalse;	// Enable the font file, but disable FontRouter.
	}

	// Normal case
	return ETrue;
}


//////////////////////////////////////////////////////////////////////////
// CRouterFontRasterizerContext
//
// ========== [START] COpenFontRasterizerContext ==========
void CRouterFontRasterizerContext::StartGlyph(TOpenFontGlyphData* aGlyphData)
{
	aGlyphData->SetPointersToInternalBuffers();
	iGlyphData = aGlyphData;
	iGlyphDataStart = iGlyphDataPtr = aGlyphData->BufferStart();
	// Allow 4 extra bytes; BITGDI requires this.
	iGlyphDataEnd = aGlyphData->BufferEnd() - 4;
	iGlyphBit = 1;
	*iGlyphDataPtr = 0;
	iBytesNeeded = 1;
	iOverflow = FALSE;
}

void CRouterFontRasterizerContext::WriteGlyphBit(TInt aBit)
{
	if (aBit && !iOverflow)
		*iGlyphDataPtr |= iGlyphBit;
	iGlyphBit <<= 1;
	if (iGlyphBit == 256)
	{
		iGlyphBit = 1;
		iBytesNeeded++;
		if (++iGlyphDataPtr < iGlyphDataEnd)
			*iGlyphDataPtr = 0;
		else
			iOverflow = TRUE;
	}
}

void CRouterFontRasterizerContext::WriteGlyphByte(TInt aByte)	// Added on Symbian OS 7
{
	if (iGlyphDataPtr < iGlyphDataEnd)
		*iGlyphDataPtr++ = (TUint8)aByte;
	else
		iOverflow = TRUE;
	iBytesNeeded++;
}

void CRouterFontRasterizerContext::EndGlyph()
{
	// Add 4 bytes to the data size; some BITGDI functions require this.
	iGlyphData->SetBytesNeeded(iBytesNeeded + 4);
	iGlyphData = NULL;
}
// ========== [END] COpenFontRasterizerContext ==========


inline void CRouterFontRasterizerContext::OpenGlyphReadStream(const TUint8 * aGlyphStreamPtr)
{
	iGlyphStreamStart = iGlyphStreamPtr = aGlyphStreamPtr;
	iReadBitMask = 1;
}

inline TInt CRouterFontRasterizerContext::CloseGlyphReadStream()
{
	TInt bytes = iGlyphStreamPtr - iGlyphStreamStart + 1;
	iGlyphStreamStart = iGlyphStreamPtr = NULL;
	return bytes;
}

inline void CRouterFontRasterizerContext::ShiftBits(TInt aNumBits)
{
	while(aNumBits--)
	{
		if (256 == iReadBitMask)
		{
			iReadBitMask = 1;
			iGlyphStreamPtr ++;
		}
		iReadBitMask <<= 1;
	}
}


inline TInt CRouterFontRasterizerContext::ReadGlyphBit()
{
	ASSERT_RET(NULL != iGlyphStreamPtr, NULL); // TODO: DO NOT panic for test purpose.
	if (256 == iReadBitMask)
	{
		iReadBitMask = 1;
		iGlyphStreamPtr ++;
	}
	TInt bit = * iGlyphStreamPtr & iReadBitMask;	// This is not the final result, as it has postfix 0s.
	iReadBitMask <<= 1;
	return (0 != bit);
}


inline TInt CRouterFontRasterizerContext::ReadGlyphBits(TInt aNumBits)		// Starting with the least significant bit
{
	TInt value = 0;
	TInt offset = 0;
	while(aNumBits--)
	{
		value += ReadGlyphBit() << offset;
		offset ++;
	}
	return value;
}


TInt CRouterFontRasterizerContext::GetBytesNeeded(TInt aCode, const TUint8 * aGlyph,
	const TOpenFontCharMetrics & aMetrics)
{
	if (NULL == aGlyph) return 0;

	TInt width = aMetrics.Width();
	TInt height = aMetrics.Height();

	if (0 == height || 0 == width) return 0;	// Empty glyph need nothing (FAULT_001).

	OpenGlyphReadStream(aGlyph);

	TInt row = 0;
	while(row < height)					// "row" will increase in the loop if needed.
	{
		TInt ind = ReadGlyphBit();				// read repeating indication bit of section header.
		TInt rowsInSection = ReadGlyphBits(4);	// Read count of (non-)repeating rows followed.
		if (0 == rowsInSection)					// Extreme bad glyph! Discard the section header.
		{
			__dbgprint(level_warning, "Bad glyph (section without rows) for %d", aCode);
			continue;
		}
		row += rowsInSection;					// Shift number of rows in current section.
		if (1 == ind)								// Non-repeat
			ShiftBits(width * rowsInSection);
		else /*if (0 == ind)*/						// Repeat
			ShiftBits(width);
	}
	if (row != height)
		__dbgprint(level_warning, "Bad glyph (height overflow) for %d", aCode);

	// Add 4 bytes to the data size; some BITGDI functions require this.
	return(CloseGlyphReadStream() + 4);
}


void CRouterFontRasterizerContext::ApplyAlgorithmicBold(TOpenFontGlyphData & aGlyphData, TInt aCode)
{
	const TOpenFontCharMetrics * metrics = aGlyphData.Metrics();
	if (NULL == metrics) return;

	const TUint8 * glyph = aGlyphData.BitmapPointer();
	if (NULL == glyph) return;

	TUint8 * buffer = aGlyphData.BufferStart();
	if (NULL == buffer) return;

	TBool share_buffer = (glyph == buffer);		// Check whether buffer is shared.

	OpenGlyphReadStream(glyph);		// Reading stream
	StartGlyph(& aGlyphData);		// Writing stream

	TInt width = metrics->Width();
	TInt height = metrics->Height();
	TInt row = 0;
	while(row < height)					// "row" will increase in the loop if needed.
	{
		// Bypass the section header (1 + 4 bites)
		TInt ind = ReadGlyphBit();				// read repeating indication bit of section header.
		WriteGlyphBit(ind);

		TInt rowsInSection = ReadGlyphBits(4);	// Read count of (non-)repeating rows followed.
		WriteGlyphBit(rowsInSection & 1);
		WriteGlyphBit(rowsInSection & 2);
		WriteGlyphBit(rowsInSection & 4);
		WriteGlyphBit(rowsInSection & 8);

		if (0 == rowsInSection)					// Extreme bad glyph! Discard the section header.
		{
			__dbgprint(level_warning, "Bad glyph (section without rows) for %d", aCode);
			continue;
		}

		row += rowsInSection;					// Shift number of rows in current section.

		TInt loop_rows = (1 == ind) ? rowsInSection : 1;
		for (TInt r = 0; r < loop_rows; r ++)
		{
			TInt last_bit = 0;
			for (TInt i = 0; i < width; i ++)
			{
				TInt bit = ReadGlyphBit();
				WriteGlyphBit(last_bit | bit);
				last_bit = bit;
			}

			if (! share_buffer)			// Add one more pixel per row if buffer is not shared.
				WriteGlyphBit(last_bit);
		}
	}

	if (row != height)
	{
		__dbgprint(level_warning, "Bad glyph (height overflow) for %d", aCode);
		// Exceptional restoration.
		aGlyphData.SetBitmapPointer(glyph);
		aGlyphData.SetMetricsPointer(metrics);
	}

	if (! share_buffer)					// Increase the width of char as one more pixel added.
	{
		aGlyphData.SetMetrics(* const_cast<TOpenFontCharMetrics *>(metrics));
		const_cast<TOpenFontCharMetrics *>(aGlyphData.Metrics())->SetWidth(metrics->Width() + 1);
		const_cast<TOpenFontCharMetrics *>(aGlyphData.Metrics())->SetHorizAdvance(metrics->HorizAdvance() + 1);
	}

	EndGlyph();
	CloseGlyphReadStream();
}



TInt CNativeFontFile::VerticalTwipsToPixels(TInt aTwipsHeight) const
{
	TInt kPixelHeightInTwips = iFontStore->iKPixelHeightInTwips;
	if (0 == kPixelHeightInTwips)
		kPixelHeightInTwips = KDefaultKPixelHeightInTwips;

	return((aTwipsHeight * 1000 + kPixelHeightInTwips / 2) / kPixelHeightInTwips);
}


CNativeFontFile::CNativeFontFile(CRouterFontRasterizer & aRasterizer, TInt aUid,
	const TDesC & aFileName, CFontStore & aFontStore)
	: COpenFontFile(aUid, aFileName), iRasterizer(aRasterizer), iFontStore(& aFontStore)
{
}


CNativeFontFile::~CNativeFontFile()
{
	delete iFontStore;
}


CNativeFontFile * CNativeFontFile::NewL(CRouterFontRasterizer & aRasterizer,
	TInt aUid, const TDesC & aFileName, const RFs & /*aFs*/)
{
	CNativeFontFile * file = NULL;

	// Add to system font store
	if (0 == aFileName.CompareF(KReservedFontFile1) || 0 == aFileName.CompareF(KReservedFontFile2))
	{
		aRasterizer.AddNativeFontFileL(aFileName);
	}

	// Create font store
	CFontStore * font_store = CFontStore::NewL(& User::Heap());
	CleanupStack::PushL(font_store);
	{
		// Add font file
		TUid uid = font_store->AddFileL(aFileName);
		if (KNullUidValue == uid.iUid)
			User::Leave(KErrGeneral);

		file = new(ELeave) CNativeFontFile(aRasterizer, aUid, aFileName, * font_store);
	}
	CleanupStack::Pop(font_store);

	CleanupStack::PushL(file);
	{
		// Duplicate the face attrib list.
		for (TInt i = 0; i < font_store->NumTypefaces(); i ++)
		{
			TTypefaceSupport typeface;
			font_store->TypefaceSupport(typeface, i);

			TOpenFontFaceAttrib face;
			TPtrC name(typeface.iTypeface.iName);
			face.SetFullName(name);
			face.SetFamilyName(name);
			face.SetLocalFullName(name);
			face.SetLocalFamilyName(name);
			face.SetMinSizeInPixels(file->VerticalTwipsToPixels(typeface.iMinHeightInTwips));
			face.SetCoverage(KMaxTUint, KMaxTUint, 0, 0);	// TODO:
			face.SetBold(KErrNotFound != name.FindF(_L("Bold")));							// TODO:
			face.SetItalic(EFalse);
			face.SetSerif(typeface.iTypeface.IsSerif());
			face.SetMonoWidth(! typeface.iTypeface.IsProportional());

			file->AddFaceL(face);
		}
	}
	CleanupStack::Pop(file);

	return file;
}


TBool CNativeFontFile::HasUnicodeCharacterL(TInt /*aFaceIndex*/, TInt /*aCode*/) const
{
	// TODO:
	return ETrue;
}


TInt CNativeFontFile::GetFaceIndex(const CFont & aFont) const
{
	TInt face_count = FaceCount();
	if (face_count <= 1)		// If no more than 1 face, return 0 directly.
		return 0;

	TFontSpec font_spec = aFont.FontSpecInTwips();
	for (TInt face_index = 0; face_index < FaceCount(); face_index ++)
	{
		const TOpenFontFaceAttrib & face = FaceAttrib(face_index);
		if (font_spec.iTypeface.iName == face.Name()
			&& VerticalTwipsToPixels(font_spec.iHeight) == face.MinSizeInPixels()
			&& font_spec.iTypeface.IsSerif() == face.IsSerif()
			&& font_spec.iTypeface.IsProportional() != face.IsMonoWidth())
		{
			return face_index;
		}
	}

	return 0;
}


void CNativeFontFile::GetNearestFontInPixelsL(RHeap * /*aHeap*/,
	COpenFontSessionCacheList * aSessionCacheList, const TOpenFontSpec & aDesiredFontSpec,
	TInt /*aPixelWidth*/, TInt /*aPixelHeight*/, COpenFont *& aFont, TOpenFontSpec & aActualFontSpec)
{
	CFont * font = NULL;

	TInt result = iFontStore->GetNearestFontInPixels(font, aDesiredFontSpec);
	if (KErrNone != result || NULL == font)
		aFont = NULL;
	else
	{
		CNativeFont * native = CNativeFont::NewL(iRasterizer, aSessionCacheList, this, GetFaceIndex(* font), * font);

		// Either desired face attribute is bold or algorithmic bold is requested, apply algorithmic bold.
		if ((aDesiredFontSpec.IsBold() && EStrokeWeightNormal == font->FontSpecInTwips().iFontStyle.StrokeWeight())
			|| TOpenFontSpec::EAlgorithmicBold == aDesiredFontSpec.Effects())
		{
			native->SetAlgorithmicBold(ETrue);
		}

		aFont = native;
		aActualFontSpec = aDesiredFontSpec;

		const TOpenFontFaceAttrib * face = aFont->FaceAttrib();
		ASSERT_RET(face, RET_VOID);

		aActualFontSpec.SetAttrib(* face);
		aActualFontSpec.SetHeight(aFont->Metrics().Size());
		aActualFontSpec.SetBitmapType(EDefaultGlyphBitmap == aDesiredFontSpec.BitmapType() ?
			EDefaultGlyphBitmap : EMonochromeGlyphBitmap);
	}
}


#ifdef __SYMBIAN_9__
void CNativeFontFile::GetNearestFontToDesignHeightInPixelsL(
	RHeap * /*aHeap*/, COpenFontSessionCacheList * aSessionCacheList,
	const TOpenFontSpec & aDesiredFontSpec, TInt /*aPixelWidth*/, TInt /*aPixelHeight*/,
	COpenFont *& aFont, TOpenFontSpec & aActualFontSpec)
{
	CFont * font = NULL;
	TInt result = iFontStore->GetNearestFontToDesignHeightInPixels(font, aDesiredFontSpec);
	if (KErrNone != result || NULL == font)
		aFont = NULL;
	else
	{
		aFont = CNativeFont::NewL(iRasterizer, aSessionCacheList, this, GetFaceIndex(* font), * font);
		const TOpenFontFaceAttrib * face = aFont->FaceAttrib();
		if (face)
			aActualFontSpec.SetAttrib(* face);
	}
}


void CNativeFontFile::GetNearestFontToMaxHeightInPixelsL(
	RHeap * /*aHeap*/, COpenFontSessionCacheList * aSessionCacheList,
	const TOpenFontSpec & aDesiredFontSpec, TInt /*aPixelWidth*/, TInt /*aPixelHeight*/,
	COpenFont *& aFont, TOpenFontSpec & aActualFontSpec, TInt aMaxHeight)
{
	CFont * font = NULL;
	TInt result = iFontStore->GetNearestFontToMaxHeightInPixels(font, aDesiredFontSpec, aMaxHeight);
	if (KErrNone != result || NULL == font)
		aFont = NULL;
	else
	{
		aFont = CNativeFont::NewL(iRasterizer, aSessionCacheList, this, GetFaceIndex(* font), * font);
		const TOpenFontFaceAttrib * face = aFont->FaceAttrib();
		if (face)
			aActualFontSpec.SetAttrib(* face);
	}
}
#endif


CNativeFont * CNativeFont::NewL(CRouterFontRasterizer & aRasterizer, COpenFontSessionCacheList * aSessionCacheList,
	COpenFontFile * aFile, TInt aFaceIndex, CFont & aFont)
{
	RHeap * heap = aRasterizer.SharedHeap();
	ASSERT_RET(heap, NULL);
	ASSERT_RET(aSessionCacheList && aFile, NULL);

	CNativeFont * native_font = (CNativeFont*) heap->AllocL(sizeof(CNativeFont));
	return (new(native_font) CNativeFont(aRasterizer, aSessionCacheList, aFile, aFaceIndex, aFont));
}


CNativeFont::CNativeFont(CRouterFontRasterizer & aRasterizer, COpenFontSessionCacheList * aSessionCacheList,
	COpenFontFile * aFile, TInt aFaceIndex, CFont & aFont)
	: COpenFont(aRasterizer.SharedHeap(), aSessionCacheList, aFile, aFaceIndex),
	iRasterizer(aRasterizer),
	//lint --e(826)
	iBitmapFont(static_cast<CBitmapFont *>(& aFont))
{
	iBitmapFont->GetFontMetrics(iMetrics);
#ifdef __SYMBIAN_9__
	iFontCapitalAscent		= iMetrics.Ascent();
	iFontMaxAscent			= iMetrics.MaxHeight();
	iFontStandardDescent	= iMetrics.Descent();
	iFontMaxDescent 		= iMetrics.MaxDepth();
	iFontLineGap			= iFontMaxAscent + iFontMaxDescent;
#endif
}


CNativeFont::~CNativeFont()
{
	CNativeFontFile * file = File();
	if (file && iBitmapFont)
	{
		file->ReleaseFont(iBitmapFont);
	}
	iBitmapFont = NULL;
	iReplCharGlyph = NULL;
}


void CNativeFont::RasterizeL(TInt aCode, TOpenFontGlyphData * aGlyphData)
{
	if (NULL == aGlyphData)
		return;

	aGlyphData->SetPointersToInternalBuffers();
	const TUint8 * bitmap = NULL;
	TOpenFontCharMetrics & char_metrics
		= * const_cast<TOpenFontCharMetrics *>(aGlyphData->Metrics());

	if (EFalse == iBitmapFont->GetCharacterData(KNullHandle, aCode, char_metrics, bitmap)
		|| NULL == bitmap)
	{
		aCode = KReplacementCharacter;
		if (EFalse == iBitmapFont->GetCharacterData(KNullHandle, aCode, char_metrics, bitmap)
			|| NULL == bitmap)
		{
			// Extreme case: Even KReplacementCharacter can not be rasterized.
			aGlyphData->BufferStart()[0] = 0;
			Mem::FillZ(& char_metrics, sizeof(TOpenFontCharMetrics));	// Clear all.
			aGlyphData->SetBytesNeeded(0);
			return;
		}
	}

	aGlyphData->SetBitmapPointer(bitmap);
	aGlyphData->SetBytesNeeded(iRasterizer.iContext->GetBytesNeeded(aCode, bitmap, char_metrics));

	// Apply all glyph post-processes.

	// Apply algorithmic bold effect if needed.
	if (iAlgorithmicBold)
		iRasterizer.iContext->ApplyAlgorithmicBold(* aGlyphData, aCode);
}



//////////////////////////////////////////////////////////////////////////
// THeapSearch
//
class RHeapExt : public RHeap
{
public:
	inline static SCell* GetCellAddress(RHeap & aHeap, TAny* aCell)
		{ return reinterpret_cast<RHeapExt &>(aHeap).DoGetAddress(aCell); }
private:
	// RHeap::GetAddress differs between version 6 and 9, so inhibite the lint notify.
	//lint -e{818, 1762}
	inline SCell* DoGetAddress(TAny * aCell) { return RHeap::GetAddress(aCell); }
	RHeapExt();
};


THeapSearch::THeapSearch(RHeap& aHeap)
	: THeapWalk(aHeap), iHeap(aHeap), iObjectFound(NULL), iRefObject(NULL),
	iCellLengthExpected(0), iCellHeaderLength(KErrNotFound), iTypeId(0)
{
}

TInt THeapSearch::Search(CBase & aRefObject)
{
	iObjectFound = NULL;
	iRefObject = & aRefObject;
	if (KErrNotFound == iCellHeaderLength)
	{
		RHeap::SCell * cell = RHeapExt::GetCellAddress(iHeap, &aRefObject);
		if (NULL == cell)	// Reference object is not in the same heap.
			cell = RHeapExt::GetCellAddress(User::Heap(), &aRefObject);
		if (NULL == cell)	// Not in the user heap either.
			return KErrArgument;

		iCellHeaderLength = (TUint8 *) (& aRefObject) - (TUint8 *) cell;
	}

	iTypeId = _typeid(aRefObject);
	iCellLengthExpected = ((RHeap::SCell *) (TAny *) ((TUint8 *) &aRefObject - iCellHeaderLength))->len;

	return Walk();
}

void THeapSearch::Info(TCellType aType,TAny *aBase,TInt aLength)
{
	CBase * object;

	if (NULL == aBase || NULL == iRefObject) return;

	switch(aType)
	{
	case EGoodAllocatedCell:
		if (aLength == iCellLengthExpected)
		{
			object = (CBase *) (TAny *) ((TUint8 *) aBase + iCellHeaderLength);
			if (object != iRefObject && _typeid(*object) == iTypeId)
			{
				if (NULL == iObjectFound)		// Only store the object found first.
				{
					iObjectFound = object;
				}
				SetValue(Value() + 1);
				DealL(object);
			}
		}
		break;

	default:
		break;
	}
}



//////////////////////////////////////////////////////////////////////////
// RSettings
//
RSettings::RSettings()
	: iEnabled(ETrue),
	iOneOff(EFalse),
	iAdaptNativeFont(ETrue),
#ifdef _DEBUG
	iLogLevel(level_debug),
#else
	iLogLevel(level_info),
#endif
	iBitmapTypeAtt(ENoChange),
	iFixFontMetricsMode(EFixFontMetricsNone),
	iFixCharMetricsMode(EFixCharMetricsNone),
	iZoomRatio(100),
	iZoomMinSize(0),
	iZoomMaxSize(KMaxTInt),
	iChroma(100),
	iFontHeightExp(EFalse),
	iFontTest(0),
	iAutoFontName(NULL),
	iDisabledFontFile(4),
	iExtraFontPath(4),
	iExtraFontFile(4),
	iGlobalSubstFont(1),
	iFontNamePool(4),
	iFontMap(4)
{
#if 0
	// Set FixFontMetricsMode according to language.
	TInt lang = ELangEnglish;
	HAL::Get(HALData::ELanguageIndex, lang);
	if (ELangTaiwanChinese == lang || ELangTaiwanChinese == lang || ELangHongKongChinese == lang)
		iFixFontMetricsMode = EFixFontMetricsNoDescent;
	else
		iFixFontMetricsMode = EFixFontMetricsNone;
#endif
}

void RSettings::LoadSettingsL(RFs& aFs, const TDesC& aDirPath, const TDesC& aFileName)
{
	// Clear font map and font name pool first.
	iFontNamePool.Reset();
	iDisabledFontFile.Reset();
	iExtraFontPath.Reset();
	iExtraFontFile.Reset();
	iGlobalSubstFont.Reset();
	iFontMap.Reset();

	// Parse configuration file.
	ParseL(aFs, aDirPath, aFileName);

	// Disable Anti-Aliase on Symbian 6
	TInt uid = 0;
	HAL::Get(HALData::EMachineUid, uid);
	switch(uid)
	{
	case 0x101F466A:	// Nokia 3650
	case 0x101F7962:	// Nokia 3650 (Product UID)
	case 0x101F4FC3:	// Nokia 7650
	case 0x101F6F87:	// Nokia 7650 (Product UID)
	case 0x101f8c19:	// Nokia N-Gage
	case 0x101F8A64:	// Nokia N-Gage (Product UID)
	case 0x101FB2B1:	// Nokia N-Gage QD
	case 0x101FA031:	// Sendo X
	case 0x101F9071:	// Siemens SX1
		iBitmapTypeAtt = ENoChange;
		__dbgprint(level_info, "Anti-aliased bitmap type is disabled on Symbian 6.");
		break;
	default:
		break;
	}

	// TODO: Log to special log file.
/*
	RDebug::Print(_L("FontNamePool:"));
	for (TInt i=0; i<iFontNamePool.Count(); i++)
	{
		TPtrC name = iFontNamePool[i];
		RDebug::Print(_L("  %S"), & name);
	}
*/
	__dbgprint(level_info, "FontMap:");
	for (TInt i=0; i<iFontMap.Count(); i++)
	{
		TFontMapEntry & entry = iFontMap[i];
		__dbgprint(level_info, "  %S@%d:%d=%S@%d:%d", & entry.iDesiredFontName,
			entry.iDesiredFontHeight, entry.iDesiredFontAttr, & entry.iRoutedFontName,
			entry.iRoutedFontHeight, entry.iRoutedFontAttr);
	}
}


TBool RSettings::IsFontFileToBeDisabled(const TDesC & aFileName) const
{
	if (iDisabledFontFile.Count() > 0)
	{
		TInt index;
		if (0 == iDisabledFontFile.FindIsq(aFileName, index))
			return ETrue;

		TParsePtrC parse(aFileName);
		if (0 == iDisabledFontFile.FindIsq(parse.NameAndExt(), index))
			return ETrue;
	}

	return EFalse;
}


TInt RSettings::GetNextExtraFontFileL(HBufC & aFontFile)
{
	if (iExtraFontFile.Count() <= 0)
		return KErrNotFound;

	aFontFile = iExtraFontFile[0];
	iExtraFontFile.Delete(0);
	return KErrNone;
}


TInt RSettings::SearchAllExtraFontFilesL(RFs & aFs)
{
	TInt count = 0;

	for (TInt i = 0; i < iExtraFontPath.Count(); i ++)
	{
		const TPtrC16 & file = iExtraFontPath[i];

		TParsePtrC parser(file);
		TFindFile * finder = new(ELeave) TFindFile(aFs);
		CleanupStack::PushL(finder);
		{
			CDir * file_list;
			TParse * entry = new(ELeave) TParse;
			CleanupStack::PushL(entry);
			{
				TInt result = finder->FindWildByDir(parser.NameAndExt(), parser.Path(), file_list);
				while (KErrNone == result)
				{
					__dbgprint(level_info, "Searching extra font files in \"%S\" ...", & finder->File());
					for (TInt i=0; i<file_list->Count(); i++)
					{
						result = entry->Set((*file_list)[i].iName, & finder->File(), NULL);
						if (KErrNone != result)
						{
							__dbgprint(level_fatal, "Failed to parse extra font file \"%S\"! (Err %d)",
								& (*file_list)[i].iName, result);
							continue;
						}

						iExtraFontFile.AppendL(entry->FullName());
						__dbgprint(level_info, "Font file \"%S\" is found.", & entry->FullName());
						count ++;
					}
					delete file_list;
					result = finder->FindWild(file_list);
				}
			}
			CleanupStack::Pop(entry);
		}
		CleanupStack::Pop(finder);
	}

	iExtraFontPath.Reset();  // Clear all extra font files.

	return count;
}


void RSettings::PoolFontNameL(TPtrC& aFontName)
{
	// Seek current font name pool.
	TInt index;
	if (0 == iFontNamePool.FindIsq(aFontName, index, ECmpNormal))
		{ aFontName.Set(iFontNamePool[index]); }
	else
		{ aFontName.Set(iFontNamePool[ iFontNamePool.InsertIsqL(aFontName, ECmpNormal) ]); }
}

TInt RSettings::ParseLineSection(const TDesC& aSectionName)
{
	if (aSectionName.CompareF(_L("Global")) == 0)	{ return EGlobal; }
	if (aSectionName.CompareF(_L("FontMap")) == 0)	{ return EFontMap; }

	__dbgprint(level_warning, "Unrecognized group name: %S", &aSectionName);
	return ENullGroup;
}

TInt RSettings::ParseLineNormalL(TInt aSection, TInt aLineNo, const TDesC& aKey, const TDesC& aValue)
{
#define parse_err {__dbgprint(level_warning, "Unrecognized value: %S=%S", &aKey, &aValue); return KErrArgument;}

	switch(aSection)
	{
	case ENullGroup:
		__dbgprint(level_warning, "Line %d is isolated and unaccepted.", aLineNo);
		return KErrNotSupported;

	case EGlobal:
		if   (0 == aKey.CompareF(_L("Enable")))
			{ if (!GetBool(aValue, iEnabled)) parse_err; }
		else if (0 == aKey.CompareF(_L("NativeFont")))
			{ if (!GetBool(aValue, iAdaptNativeFont)) parse_err; }
		else if (0 == aKey.CompareF(_L("LogLevel")))
			{ if (!GetInt(aValue, iLogLevel)) parse_err; }
		else if (0 == aKey.CompareF(_L("FontHeightExp")))
			{ if (!GetBool(aValue, iFontHeightExp)) parse_err; }
		else if (0 == aKey.CompareF(_L("FontTest")))
			{ if (!GetInt(aValue, iFontTest)) parse_err; }
		else if (0 == aKey.CompareF(_L("ForceAntiAliased")))	// deprecated
			{ if (!GetInt(aValue, iBitmapTypeAtt)) parse_err; }
		else if (0 == aKey.CompareF(_L("BitmapType")))
			{ if (!GetInt(aValue, iBitmapTypeAtt)) parse_err; }

		else if (0 == aKey.CompareF(_L("FixFontMetrics")))
			{ if (!GetInt(aValue, iFixFontMetricsMode)) parse_err; }
		else if (0 == aKey.CompareF(_L("FixCharMetrics")))
			{ if (!GetInt(aValue, iFixCharMetricsMode)) parse_err; }

		else if (0 == aKey.CompareF(_L("ZoomRatio")))
			{ if ((!GetInt(aValue, iZoomRatio)) || iZoomRatio < 1) parse_err; }
		else if (0 == aKey.CompareF(_L("ZoomMinSize")))
			{ if ((!GetInt(aValue, iZoomMinSize)) || iZoomMinSize < 0) parse_err; }
		else if (0 == aKey.CompareF(_L("ZoomMaxSize")))
			{ if ((!GetInt(aValue, iZoomMaxSize)) || iZoomMaxSize < iZoomMinSize) parse_err; }
		else if (0 == aKey.CompareF(_L("Chroma")))
			{ if (!GetInt(aValue, iChroma)) parse_err; }

		else if (0 == aKey.CompareF(_L("ExtraFontFile")))
			{ iExtraFontPath.InsertIsqAllowDuplicatesL(aValue); }
		else if (0 == aKey.CompareF(_L("DisableFontFile")))
			{ iDisabledFontFile.InsertIsqAllowDuplicatesL(aValue); }
		else if (0 == aKey.CompareF(_L("SubstFont")))
			{ iGlobalSubstFont.InsertIsqAllowDuplicatesL(aValue); }
		else if (0 == aKey.CompareF(_L("OneOff")))
			{ if (!GetBool(aValue, iOneOff)) parse_err; }
		else	// Unrecognized line.
		{
			__dbgprint(level_warning, "Unrecognized line: %S=%S", &aKey, &aValue);
			return KErrNotSupported;
		}

		return KErrNone;

	case EFontMap:
		{
			TFontMapEntry fontMapEntry;
			// Save font map in key sequence. Trick: compare combination of members as text.
			// Key: All desired font info.
			TKeyArrayFix key((TUint8*) &fontMapEntry.iDesiredFontName - (TUint8*) &fontMapEntry,
				ECmpNormal8,
				(TUint8*) &fontMapEntry.iRoutedFontName - (TUint8*) &fontMapEntry.iDesiredFontName);

			// Parse desired font string before '='.
			if (aKey.Length())
			{
				ParseFontString(aKey, fontMapEntry.iDesiredFontName,
					fontMapEntry.iDesiredFontHeight, fontMapEntry.iDesiredFontAttr);
			}
			// [2007.3.27] FIX: Move PoolFontNameL() out of "if" block to pool empty name also.
			PoolFontNameL(fontMapEntry.iDesiredFontName);  // Pool font name (including empty name).

			if (0 == aValue.Length())	// Empty after '='.
			{
				// This means the desired font should not be handled by FontRouter.
				iFontMap.InsertIsqAllowDuplicatesL(fontMapEntry, key);
				return KErrNone;
			}

			// Parse routed font string after '='.
			TPtrC priRoutedFontString;
			TPtrC secRoutedFontString;
			TInt posComma = aValue.Locate(',');
			if (KErrNotFound == posComma)	// Only one routed font
			{
				priRoutedFontString.Set(aValue);
			}
			else							// Both primary and secondary routed font
			{
				priRoutedFontString.Set(aValue.Left(posComma));
				secRoutedFontString.Set(aValue.Mid(posComma + 1));
				// Ignore more commas
				TInt posMoreComma = secRoutedFontString.Locate(',');
				if (posMoreComma != KErrNotFound)
					{ secRoutedFontString.Set(secRoutedFontString.Left(posMoreComma)); }
			}

			// Parse primary routed font string before comma.
			if (priRoutedFontString.Length())
			{
				ParseFontString(priRoutedFontString, fontMapEntry.iRoutedFontName,
					fontMapEntry.iRoutedFontHeight, fontMapEntry.iRoutedFontAttr,
					&fontMapEntry.iRoutedFontAttrDetail);
				PoolFontNameL(fontMapEntry.iRoutedFontName);	// Pool font name.
				iFontMap.InsertIsqAllowDuplicatesL(fontMapEntry, key);
			}

			// Keep desired font info.
			fontMapEntry.ResetRoutedFontInfo();
			// Parse secondary routed font string after comma.
			if (secRoutedFontString.Length())
			{
				ParseFontString(secRoutedFontString, fontMapEntry.iRoutedFontName,
					fontMapEntry.iRoutedFontHeight, fontMapEntry.iRoutedFontAttr,
					&fontMapEntry.iRoutedFontAttrDetail);
				// As the key is same, this will be inserted just after primary routed font may entry.
				PoolFontNameL(fontMapEntry.iRoutedFontName);	// Pool font name.
				iFontMap.InsertIsqAllowDuplicatesL(fontMapEntry, key);
			}
			return KErrNone;
		}
// TODO: Complete
#if 0
		aFontMap.strDesiredFont.Copy(aLine.Mid(0, iPosEqual));
		TPtrC strFontPair = aLine.Mid(iPosEqual + 1);
		if (strFontPair.Size() != 0){	// strFontPair with null value means skip this desired font
			if ((iPosSplitter1 = strFontPair.Locate(',')) == KErrNotFound){
				// Without routing font
				aFontMap.strNativeFont.Copy(strFontPair);
			}else{
				// With routing font
				aFontMap.strNativeFont.Copy(strFontPair.Mid(0, iPosSplitter1));
				if ((iPosSplitter2 = strFontPair.Mid(iPosSplitter1 + 1).Locate(',')) == KErrNotFound){
					// Without HorizBearingY_Fix
					aFontMap.strRoutedFont.Copy(strFontPair.Mid(iPosSplitter1 + 1));
				}else{
					// With HorizBearingY_Fix
					aFontMap.strRoutedFont.Copy(strFontPair.Mid(iPosSplitter1 + 1, iPosSplitter2));
					TLex aHBYFLex(strFontPair.Mid(iPosSplitter1 + iPosSplitter2 + 2));
					TBool bNegative = EFalse;
					if (aHBYFLex.Peek() == '-'){
						bNegative = ETrue;
						aHBYFLex.Inc();
					}
					if (aHBYFLex.Val(aFontMap.iHorizBearingY_Fix) != KErrNone){
						aFontMap.iHorizBearingY_Fix = KHBYF_UNDEFINED;
					}else if (bNegative){
						aFontMap.iHorizBearingY_Fix = -aFontMap.iHorizBearingY_Fix;
					}
				}
			}
		}
		if (m_pFontMap == NULL){
			m_pFontMap = new (ELeave) CArrayFixFlat<TFontMap> (5);
		}
		m_pFontMap->AppendL(aFontMap);
		return KErrNone;
#endif
	default:
		__dbgprint(level_warning, "Unknown section no: %d", aSection);

	}

	return KErrUnknown;
}

void RSettings::ParseFontString(const TDesC& aFontString, TPtrC& aFontName, TInt& aFontHeight,
	TInt& aFontAttr, RSettings::TRoutedFontAtt* aRoutedFontAttDetail)
{
	// Example: LatinBold12@12:Y1ai
	aFontAttr = 0;

	// Parse desired font name, height and attributes.
	TInt posAt = aFontString.Locate('@');
	TInt posColon = aFontString.Locate(':');
	// Trick: ((TUint) KErrNone) is largest unsigned number
	TInt posFirst = ((TUint) posAt < (TUint)posColon) ? posAt : posColon;
	if (KErrNotFound == posFirst)	// Only font name preset
	{
		aFontName.Set(aFontString);
		return;
	}

	aFontName.Set(aFontString.Left(posFirst));
	// Directly parse the whole string after '@' for font height, since the tail is ignored.
	if ( KErrNotFound == posAt
		|| EFalse == GetInt(aFontString.Mid(posAt+1), aFontHeight)
		|| aFontHeight < 0 )
	{	// empty, bad or negative height
		aFontHeight = KErrNotFound;
	}

	// Parse the font attributes. (must be the tail of font string)
	if (KErrNotFound != posColon)
	{
		TLex params(aFontString.Mid(posColon+1));
		TChar param;
		while(!params.Eos())
		{
			TInt result;
			param = params.Get();
			switch(param)
			{
			case '@':	return;		// This means ':' is before '@'
			// Mutual exclusion
			case 'i':	aFontAttr &= ~EFontAttItalic;		aFontAttr |= EFontAttUpright;	break;
			case 'I':	aFontAttr &= ~EFontAttUpright;		aFontAttr |= EFontAttItalic;	break;
			case 'b':	aFontAttr &= ~EFontAttBoldAlways;	aFontAttr |= EFontAttBoldOnDemand;		break;
			case 'B':	aFontAttr &= ~EFontAttBoldOnDemand;	aFontAttr |= EFontAttBoldAlways;		break;
			case 'a':	aFontAttr &= ~EFontAttAntiAliased;	aFontAttr |= EFontAttMonochrome;	break;
			case 'A':	aFontAttr &= ~EFontAttMonochrome;	aFontAttr |= EFontAttAntiAliased;	break;

			case 'E':	aFontAttr |= EFontAttHeightExpand;	break;

			case 'Y':
				// Directly parse the whole string after 'Y'.
				if (aRoutedFontAttDetail)
				{
					result = params.Val(aRoutedFontAttDetail->iYAdjust);
					if (KErrNone == result) { aFontAttr |= EFontAttYAdjust; }
					else { __dbgprint(level_warning, "Error (%d) while parsing YAdjust.", result); }
				}
				break;

			case 'W':
				if (aRoutedFontAttDetail)
				{
					result = params.Val(aRoutedFontAttDetail->iCharGapAdjust);
					if (KErrNone == result) { aFontAttr |= EFontAttCharGapAdjust; }
					else { __dbgprint(level_warning, "Error (%d) while parsing CharWidthAdjust.", result); }
				}
				break;

			case 'L':
				if (aRoutedFontAttDetail)
				{
					result = params.Val(aRoutedFontAttDetail->iLineGapAdjust);
					if (KErrNone == result) { aFontAttr |= EFontAttLineGapAdjust; }
					else { __dbgprint(level_warning, "Error (%d) while parsing LineGapAdjust.", result); }
				}
				break;

			case 'C':
				if (aRoutedFontAttDetail)
				{
					result = params.Val(aRoutedFontAttDetail->iChromaAdjust);
					if (KErrNone == result) { aFontAttr |= EFontAttChromaAdjust; }
					else { __dbgprint(level_warning, "Error (%d) while parsing ChromaAdjust.", result); }
				}
				break;

			case 'Z':
				if (aRoutedFontAttDetail)
				{
					result = params.Val(aRoutedFontAttDetail->iZoomRatio);
					if (KErrNone == result) { aFontAttr |= EFontAttZoom; }
					else { __dbgprint(level_warning, "Error (%d) while parsing Zoom.", result); }
				}
				break;

			default:
				// TODO:
				break;
			}
		}
	}
}

// Output parameter aPrimaryFontMapEntry/aSecondaryFontMapEntry will not be altered when query failed.
TInt RSettings::QueryFontMap(const TOpenFontSpec& aDesiredFontSpec,
	const TFontMapEntry *& aFontMapEntry) const
{
	// TODO: Support font attrib on desired font.
	TBuf<200> debugBuf;
	TPtrC fontName(aDesiredFontSpec.Name());
	TBuf<4> styleText;
	if (aDesiredFontSpec.IsSymbol()) styleText.Append('#');
	if (aDesiredFontSpec.IsBold()) styleText.Append('B');
	if (aDesiredFontSpec.IsItalic()) styleText.Append('I');
	if (aDesiredFontSpec.IsSerif()) styleText.Append('S');
	if (aDesiredFontSpec.IsMonoWidth()) styleText.Append('M');
	
	debugBuf.Format(_L("REQ [%S S<%S> @ %d T%d]"), & fontName, & styleText, aDesiredFontSpec.Height(),
		aDesiredFontSpec.BitmapType());

	TInt result = KErrNotFound;
	//lint -e{717} "do ... while(0);"
	do
	{
		TInt index;
		if (0 == iFontMap.Count())	// No font map
			break;

		if (0 != iFontNamePool.FindIsq(aDesiredFontSpec.Name(), index, ECmpNormal))
			if (0 != iFontNamePool.FindIsq(KFontNameWildcard, index, ECmpNormal))
				break;

		TFontMapEntry fontMapEntry;
		fontMapEntry.iDesiredFontName.Set(iFontNamePool[index]);
		fontMapEntry.iDesiredFontHeight = aDesiredFontSpec.Height();
		TInt fontAttr = 0;
		if (aDesiredFontSpec.IsBold() || (aDesiredFontSpec.Effects() & TOpenFontSpec::EAlgorithmicBold) != 0) {
			fontAttr |= EFontAttBoldAlways;
		}
		//if (aDesiredFontSpec.IsItalic()) {
		//	fontAttr |= EFontAttItalic;
		//}
		fontMapEntry.iDesiredFontAttr = fontAttr;

		// TODO: Implement desired font attributes.
		// Key: All desired font info.
		TKeyArrayFix key((TUint8*) &fontMapEntry.iDesiredFontName - (TUint8*) &fontMapEntry,
			ECmpNormal8,
			(TUint8*) &fontMapEntry.iRoutedFontName - (TUint8*) &fontMapEntry.iDesiredFontName);

		if (0 != iFontMap.FindIsq(fontMapEntry, key, index))
		{	// Font with given height cannot be found, search again without height specified.

			fontMapEntry.iDesiredFontHeight = KErrNotFound;
			if (0 != iFontMap.FindIsq(fontMapEntry, key, index)) {
				__dbgprint(level_info, "desired attr not found %d", fontMapEntry.iDesiredFontAttr);
				fontMapEntry.iDesiredFontAttr = 0;
				key = TKeyArrayFix((TUint8*) &fontMapEntry.iDesiredFontName - (TUint8*) &fontMapEntry,
					ECmpNormal8,
					(TUint8*) &fontMapEntry.iDesiredFontAttr - (TUint8*) &fontMapEntry.iDesiredFontName);
							// ~~~~~~~~~~ iDesiredFontAttr is ignored in font-map match.
				if (0 != iFontMap.FindIsq(fontMapEntry, key, index))
					break;
			}
		}

		aFontMapEntry = & iFontMap[index];

		if (0 == aFontMapEntry->iRoutedFontName.Length())
		{
			debugBuf.Append(_L(" Bypassed"));
		}
		else
		{
			fontName.Set(aFontMapEntry->iRoutedFontName);
			debugBuf.AppendFormat(_L(" => [%S @ %d : %d]"), & fontName,
				aFontMapEntry->iRoutedFontHeight, aFontMapEntry->iRoutedFontAttr);
		}

		result = KErrNone;
	} while (0);

	if (KErrNotFound == result)
		debugBuf.Append(_L(" without FontMap"));

	RDebugHelper::Log(debugBuf);

	return result;
}


inline const HBufC * RSettings::GetAutoFontName() const
{
	return iAutoFontName;
}


inline void RSettings::SetAutoFontNameL(const TDesC & aFontName)
{
	delete iAutoFontName;
	iAutoFontName = aFontName.AllocL();
}


void RSettings::ApplyFontAttr(const TFontMapEntry * aFontMapEntry,
	TOpenFontSpec & aOpenFontSpec) const
{
	if (aFontMapEntry)
	{	// Apply basic attributes
		if (aFontMapEntry->iRoutedFontName == KFontNameWildcard)
		{
			const HBufC* auto_font_name = GetAutoFontName();
			if (auto_font_name)
				aOpenFontSpec.SetName(* auto_font_name);
			// DO NOT change font name when iAutoFontName is invalid.
		}
		else
		{
			aOpenFontSpec.SetName(aFontMapEntry->iRoutedFontName);
		}

		TInt attr = aFontMapEntry->iRoutedFontAttr;

		if (KErrNotFound != aFontMapEntry->iRoutedFontHeight)
			aOpenFontSpec.SetHeight(aFontMapEntry->iRoutedFontHeight);
		else if (attr & EFontAttZoom)
			aOpenFontSpec.SetHeight(aOpenFontSpec.Height() * aFontMapEntry->iRoutedFontAttrDetail.iZoomRatio / 100);

		// Apply misc. attributes.
		if (attr & EFontAttBoldAlways)
			{ aOpenFontSpec.SetEffects(TOpenFontSpec::EAlgorithmicBold); }
		else if ((attr & EFontAttBoldOnDemand) && aOpenFontSpec.IsBold())
			{ aOpenFontSpec.SetEffects(TOpenFontSpec::EAlgorithmicBold); }

		if (attr & EFontAttItalic)
			{ aOpenFontSpec.SetItalic(ETrue); }
		else if (attr & EFontAttUpright)
			{aOpenFontSpec.SetItalic(EFalse); }

		if (attr & EFontAttAntiAliased)
			{ aOpenFontSpec.SetBitmapType(EAntiAliasedGlyphBitmap); }
		else if (attr & EFontAttMonochrome)
			{ aOpenFontSpec.SetBitmapType(EMonochromeGlyphBitmap); }
		else
			ApplyGlobalBitmapType(aOpenFontSpec);
	}
	else
		ApplyGlobalBitmapType(aOpenFontSpec);

	if (100 != iZoomRatio
		&& aOpenFontSpec.Height() >= iZoomMinSize && aOpenFontSpec.Height() <= iZoomMaxSize)
	{
		aOpenFontSpec.SetHeight(aOpenFontSpec.Height() * iZoomRatio / 100);
	}
}


void RSettings::ApplyGlobalBitmapType(TOpenFontSpec & aOpenFontSpec) const
{
	if (iBitmapTypeAtt <= ENoChange)
	{
		if (aOpenFontSpec.BitmapType() > 5)	// Reserve 2 enums for possible future type.
		{	// Desired bitmap type is not supported.
			// Caution: Symbian 6.0 (N9200) does not use this field, and leave it a random value.
			__dbgprint(level_debug, "  Bitmap type not supported, use default instead.");
			//const_cast<TOpenFontSpec&>(aDesiredFontSpec).SetBitmapType(EMonochromeGlyphBitmap);
			aOpenFontSpec.SetBitmapType(EDefaultGlyphBitmap);
		}
	}
	else if (0 == iBitmapTypeAtt % 2)		// Force XXX glyph bitmap type
	{
		aOpenFontSpec.SetBitmapType((TGlyphBitmapType) (iBitmapTypeAtt / 2));
	}
	else// if (0 != iBitmapTypeAtt % 2)
	{
		if (EDefaultGlyphBitmap == aOpenFontSpec.BitmapType())
			aOpenFontSpec.SetBitmapType((TGlyphBitmapType) ((iBitmapTypeAtt + 1) / 2));
	}
}


#ifdef __SYMBIAN_9__
// Define the interface UIDs
const TImplementationProxy ImplementationTable[] =
	{ IMPLEMENTATION_PROXY_ENTRY(KUidFontRouter, CRouterFontRasterizer::NewL) };

EXPORT_C const TImplementationProxy * ImplementationGroupProxy(TInt& aTableCount)
{
	aTableCount = sizeof(ImplementationTable) / sizeof(* ImplementationTable);

	return ImplementationTable;
}
#endif
