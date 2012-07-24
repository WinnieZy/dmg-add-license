/*
 *  main.c
 *
 *  Created by Rainer Brockerhoff on 29/4/08.
 *  Copyright 2008-2010 Rainer Brockerhoff. All rights reserved.
 *
 *  This command-line utility adds one license at a time to unflattened disk images.
 *  It's meant to be used inside build scripts.
 *
 *  Usage: AddLicense /path/to/TheUnflattened.dmg Language /path/to/TheLicense.rtf
 *
 *  The language parameter can be either short (en) or long (English) form.
 *  The strings for each language are inside the license.plist file.
 *  There must be one number and 9 strings for every language; the number is the output string
 *  encoding (kTextEncodingMacXYZ in TextCommon.h). The string group is actually indexed
 *  by the ISO short language name. All strings are converted to the encoding
 *  specified; usually this should be MacRoman.
 *
 *  This should run on 10.5 and up.
 *  
 */

#include <Carbon/Carbon.h>
#include <mach-o/getsect.h>

// When referencing resource layouts, always make sure of 68K aligment
#pragma options align=mac68k
typedef struct LPic {
	UInt16 defLang;
	UInt16 count;
	struct {
		UInt16 lang;
		UInt16 resid;
		UInt16 twobyte;
	} item[1];
} LPic;

typedef struct STRn {
	UInt16 count;
	UInt8 pstr;
} STRn;
#pragma options align=reset

int main(int argc, char *argv[]) {
	int result = 0;
	int i,j;
//	First we get the strings dictionary...
	unsigned long licpsize = 0;
	char* licplist = getsectdata("__TEXT", "__license_plist", &licpsize);
	if (!licplist) {
		printf("No license dictionary found!\n");
		return 1;
	}
	CFDataRef licpdata = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, (UInt8*)licplist, licpsize, kCFAllocatorNull);
	CFPropertyListRef licdict = CFPropertyListCreateFromXMLData(kCFAllocatorDefault, licpdata, kCFPropertyListImmutable, NULL);
//	On 10.6 this should use the newer API:
//	CFPropertyListRef licdict = CFPropertyListCreateWithData(kCFAllocatorDefault, licpdata, kCFPropertyListImmutable, NULL, NULL);
	if (!licdict||(CFGetTypeID(licdict)!=CFDictionaryGetTypeID())) {
		printf("Unreadable license dictionary\n");
		return 1;
	}
//	If not 4 arguments (first argument is the executable path, remember), print help text.
	if (argc!=4) {
		printf("Add one license at a time to a (unflattened) disk image.\n\n"
			   "Usage: AddLicense /path/to/TheUnflattened.dmg Language /path/to/TheLicense.rtf\n"
			   "\n\tLanguages supported:");
		j = CFDictionaryGetCount(licdict);
		CFStringRef* keys = calloc(j,sizeof(CFStringRef));
		CFDictionaryGetKeysAndValues(licdict, (const void**)keys, NULL);
		char* buffer = malloc(256);
		for (i=0;i<j;i++) {
			if (CFStringGetCString(keys[i], buffer, 255, kCFStringEncodingUTF8)) {
				printf(" %s",buffer);
			}
		}
		printf("\n\t\tYou can also use long equivalents like English, French etc.\n"
			   "\t\tThe first language added will be the default language (usually English).\n\n"
			   "Here's an actual usage example from a build script:\n"
			   "\thdiutil unflatten \"$SOURCE_ROOT/My.dmg\"\n"
			   "\t\"$BUILT_PRODUCTS_DIR/AddLicense\" \"$SOURCE_ROOT/My.dmg\" English \"$SOURCE_ROOT/EnglishLicense.rtf\"\n"
			   "\t\"$BUILT_PRODUCTS_DIR/AddLicense\" \"$SOURCE_ROOT/My.dmg\" French \"$SOURCE_ROOT/FrenchLicense.rtf\"\n"
			   "\thdiutil flatten \"$SOURCE_ROOT/My.dmg\"\n\n");
		return 0;
	}
//	Now get and check the language code.
	CFStringRef langcode = CFStringCreateWithCString(kCFAllocatorDefault, argv[2], kCFStringEncodingUTF8);
	if (!langcode) {
		printf("Unreadable language code\n");
		return 1;
	}
//	Make sure we have the canonical ISO language code here.
	CFStringRef iso = CFLocaleCreateCanonicalLanguageIdentifierFromString(kCFAllocatorDefault, langcode);
	if (!iso) {
		printf("'%s' is not a language\n",argv[2]);
		return 1;
	}
//	We also need a Pascal string representation of the ISO code. Since the buffer is zeroed,
//	isoch+1 will always be a valid C string, too.
	char* isoch = calloc(65, 1);
	if (!CFStringGetCString(iso, isoch+1, 63, kCFStringEncodingMacRoman)) {
		printf("ISO code too long or not encodeable\n");	// This should of course never happen
		return 1;
	}
	isoch[0] = strlen(isoch+1);
// We also need the language and region codes.
	LangCode lang = 0;
	RegionCode region = 0;
	OSStatus err = LocaleStringToLangAndRegionCodes(isoch+1, &lang, &region);
	if (err!=noErr) {
		printf("No language/region number for '%s'\n",isoch+1);
		return 1;
	}
//	Now we get the language encoding and strings from our dictionary
	CFArrayRef strings = CFDictionaryGetValue(licdict, iso);
	if (!strings||(CFGetTypeID(strings)!=CFArrayGetTypeID())) {
		printf("'%s (%s)' is not in the dictionary\n",argv[2],isoch+1);
		return 1;
	}
	if (CFArrayGetCount(strings)!=10) {
		printf("'%s' dictionary strings error (should be 10 items)\n",isoch+1);
		return 1;
	}
	UInt32 encoding = kTextEncodingMacRoman;
	CFNumberRef enc = CFArrayGetValueAtIndex(strings, 0);
	if (CFGetTypeID(enc)!=CFNumberGetTypeID()) {
		printf("'%s' dictionary strings error (first item must be a number)\n",isoch+1);
		return 1;
	}
//	Set up the STR* resource here. No worries about RAM, so we do a sufficiently large size,
//	and whittle it down later.
	Handle strsh = NewHandleClear(32768);
	STRn* strs = (STRn*)(*strsh);
//	We could hard-code j = strs->count = 10 here, of course...
	j = strs->count = CFArrayGetCount(strings)-1;	// OSSwapHostToBigInt16() not needed, the system will flip it
	UInt8* p = &strs->pstr;
	for (i=1;i<=j;i++) {
		CFStringRef s = CFArrayGetValueAtIndex(strings, i);
		if (CFGetTypeID(s)!=CFStringGetTypeID()) {
			printf("'%s' dictionary error (#%d is not a string)\n",isoch+1,i);
			return 1;
		}
		if (!CFStringGetPascalString(s, p, 256, encoding)) {
			printf("'%s' dictionary error (can't convert #%d to a Pascal string)\n",isoch+1,i);
			return 1;
		}
		p += p[0]+1;
	}
	SetHandleSize(strsh, p-(UInt8*)strs);
//	Set up the RTF resource.
	Handle rtfb = NULL;
	int fd = open(argv[3], O_RDONLY);
	if (fd>=0) {
		size_t rtfs = lseek(fd, 0, SEEK_END);
		if (rtfs>1024*1024) {	// 1MB ought to be enough for everybody!
			printf("'%s' is over 1MB long\n",argv[3]);
			close(fd);
			return 1;
		}
		rtfb = NewHandle(rtfs);
		lseek(fd, 0, SEEK_SET);
		size_t actual = read(fd, *rtfb, rtfs);
		close(fd);
		if (actual!=rtfs) {
			printf("Error reading '%s'\n",argv[3]);
			return 1;
		}
	}
// Open the disk image (or create it if not there, useful for debugging only)
	FSRef ref;
	HFSUniStr255 xfork = {-1};
	FSGetResourceForkName(&xfork);
	ResFileRefNum rref = kResFileNotOpened;
	if (FSPathMakeRef((UInt8*)argv[1],&ref,nil)!=noErr) {
// If we didn't get the FSRef the file doesn't exist, have to create it
		int ofd = open(argv[1],O_RDWR|O_CREAT,00700);
		if (ofd>=0) {
			close(ofd);
		} else {
			printf("Couldn't create output file '%s'\n",argv[1]);
			return 1;
		}
//	Now try again to get the FSRef
		if (FSPathMakeRef((UInt8*)argv[1],&ref,nil)!=noErr) {
			printf("Failed to get FSRef for output file '%s'\n",argv[1]);
			return 1;
		} else {
			printf("Created empty output file '%s'\n",argv[1]);
		}
	}
//	We try to create the resource fork, this will fail silently if it's already there
	FSCreateResourceFork(&ref, xfork.length, xfork.unicode, 0);
// Try to open the resource fork
	if ((FSOpenResourceFile(&ref,xfork.length,xfork.unicode,fsRdWrPerm,&rref)!=noErr)||(rref==kResFileNotOpened)) {
		printf("Failed to open resource fork of '%s'\n",argv[1]);
		return 1;
	}
//	Now we set up the LPic resource, or expand it if it already exists
	Handle lpich = Get1Resource('LPic', 5000);
	LPic* lpic = NULL;
	SInt16 resid = 5000;
	if (lpich) {
		lpic = (LPic*)(*lpich);
		j = OSSwapBigToHostInt16(lpic->count);	// No flipper for LPic resources
//	Seatch existing LPic
		for (i=0;i<j;i++) {
			SInt16 rid = OSSwapBigToHostInt16(lpic->item[i].resid)+5000;
			if (lpic->item[i].lang==OSSwapBigToHostInt16(region)) {
				resid = rid;
				goto doit;	// Substitute existing license
			}
			if (resid<=rid) {
				resid = rid+1;
			}
		}
//	Insert new license
		SetHandleSize(lpich, 2*sizeof(UInt16)+(j+1)*3*sizeof(UInt16));
		lpic = (LPic*)(*lpich);
		lpic->count = OSSwapHostToBigInt16(j+1);
	} else {
//	Create new LPic and add the license
		lpich = NewHandleClear(sizeof(LPic));
		AddResource(lpich, 'LPic', 5000, "\p");
		lpic = (LPic*)(*lpich);
		lpic->count = OSSwapHostToBigInt16(1);
		j = 0;
	}
//	Store the indexes and write the LPic
	lpic->item[j].lang = OSSwapHostToBigInt16(region);
	lpic->item[j].resid = OSSwapHostToBigInt16(resid-5000);
	lpic->item[j].twobyte = OSSwapHostToBigInt16(encoding>0?1:0);
	ChangedResource(lpich);
	WriteResource(lpich);
	printf("Wrote 'LPic' with %d licenses\n",(int)OSSwapBigToHostInt16(lpic->count));
doit:;
//	Write the RTF
	Handle old = Get1Resource('RTF ', resid);
	if (old) {
		RemoveResource(old);
	}
	AddResource(rtfb, 'RTF ', resid, (UInt8*)isoch);
	WriteResource(rtfb);
	printf("Wrote 'RTF '#%d for '%s(%s)'\n",(int)resid,argv[2],isoch+1);
//	Write the STR#
	old = Get1Resource('STR#', resid);
	if (old) {
		RemoveResource(old);
	}
	AddResource(strsh, 'STR#', resid, (UInt8*)isoch);
	WriteResource(strsh);
	printf("Wrote 'STR#'#%d for '%s(%s)', LangCode = %d, RegionCode = %d\n",(int)resid,argv[2],isoch+1,lang,region);
	UpdateResFile(rref);
	CloseResFile(rref);
	return result;
}
