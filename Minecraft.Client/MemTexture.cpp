#include "stdafx.h"
#include "MemTexture.h"
#include "MemTextureProcessor.h"

MemTexture::MemTexture(const wstring& _url, PBYTE pbData,DWORD dwBytes, MemTextureProcessor *processor)
{
	// 4J - added
    count = 1;
    id = -1;
    isLoaded = false;
	ticksSinceLastUse = 0;

	// 4J - TODO - actually implement

	// load the texture, and process it
	//loadedImage=Textures::getTexture()
	// 4J - remember to add deletes in here for any created BufferedImages when implemented
	BufferedImage *sourceImage = new BufferedImage(pbData,dwBytes);
	if(processor==NULL)
	{
		loadedImage = sourceImage;
	}
	else
	{
		BufferedImage *processedImage = processor->process(sourceImage);
		if(processedImage != NULL)
		{
			loadedImage = processedImage;
			delete sourceImage;
		}
		else
		{
			loadedImage = sourceImage;
		}
	}
	

}

MemTexture::~MemTexture()
{
	delete loadedImage;
}
