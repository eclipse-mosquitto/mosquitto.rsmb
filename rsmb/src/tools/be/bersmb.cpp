/*******************************************************************************
 * Copyright (c) 2008, 2013 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs, Nicholas O'Leary - initial API and implementation and/or initial documentation
 *******************************************************************************/

/**
 * bersmb.c - BE memory extension for viewing RSMB Heapdumps
 *
 * The RSMB heapdump is formatted as follow:
 * byte 0 - 3          : sizeof BrokerStates data structure
 * byte 4 - b          : BrokerStates structure
 * byte b+8 - b+11     : Config file name pointer
 * next 4 bytes        : sizeof Socket data structure
 * next s bytes        : sockets structure
 * next 4 bytes        : sizeof MQTTProtocol data structure
 * next m bytes        : MQTTProtocol data structure

 * then                : heap elements
 *
 * heap elements are encoded as:
 * byte i - i+3 (i+7, 64-bit) : element address
 * byte i+4 - i+7    : element size (s)
 * byte i+8 - i+8+s  : data
 *
 * When initialised, this extension reads in the heapdump and
 * generates a lookup table of heap memory locations to file locations.
 */

#ifdef NO_CINCLUDES
  #include <stdio.h>
  #include <ctype.h>
  #include <string.h>
  #include <stddef.h>
  #include <stdlib.h>
#else
  #include <cstdio>
  #include <cctype>
  #include <cstring>
  #include <cstddef>
  #include <cstdlib>
#endif

#include "bememext.h"


class MemList
{
private:

	class MemEntry
	{
	public:
		MemEntry *previous;
		MemEntry *next;
		BEMEMADDR64 memoryaddr;
		long fileaddr;
		int size;

		MemEntry(long aFileaddr, BEMEMADDR64 aMemoryaddr, int aSize)
		{
			previous = next = NULL;
			memoryaddr = aMemoryaddr;
			fileaddr = aFileaddr;
			size = aSize;
		}

	};

	MemEntry* head;
	MemEntry* tail;
	int size;

public:

   MemList()
   {
	   head = tail = NULL;
	   size = 0;
   }

   void Add(long fileaddr, BEMEMADDR64 memoryaddr, int aSize)
   {
	   MemEntry* entry = new MemEntry(fileaddr, memoryaddr, aSize);

	   if (size == 0)
	   {
		   head = entry;
		   tail = entry;
		   entry->previous = 0;
		   entry->next = 0;
	   }
	   else
	   {
		   MemEntry* current = head;
		   while (current != 0)
		   {
			   if (current->memoryaddr > entry->memoryaddr)
			   {
				   entry->next = current;
				   if (current == head)
					   head = entry;
				   else
				   {
					   entry->previous = current->previous;
					   entry->previous->next = entry;
				   }
				   current->previous = entry;
				   break;
			   }
			   else
			   {
				   current = current->next;
			   }
		   }
		   if (current == 0)
		   {
			   tail->next = entry;
			   entry->previous = tail;
			   entry->next = 0;
			   tail = entry;
		   }
	   }
	   size++;
   }


   void Dump()
   {
	   printf("------------\n");
	   MemEntry* entry = head;
	   while (entry != NULL)
	   {
		   printf("f0x%lX = m[0x%llX]s[%d]\n", entry->fileaddr, entry->memoryaddr, entry->size);
		   entry = entry->next;
	   }
	   printf("------------\n");
   }


   /*
    * The mem pointer could be anywhere in memory
    */
   long GetFileAddr(BEMEMADDR64 mem)
   {
	   MemEntry* entry = head;
	   while (entry != NULL && (entry->memoryaddr + entry->size <= mem))
		   entry = entry->next;
	   if (entry == 0)
		   return -1;
	   if (entry->memoryaddr > mem) /* mem is inbetween blocks */
		   return -1;
	   return entry->fileaddr + (mem - entry->memoryaddr);
   }

};


class BeMem
{
	FILE *fp;
	BEMEMADDR64 base_addr;
	Boolean read_only;
	MemList mem_list;

public:

	BeMem(FILE *fp, BEMEMADDR32 base_addr, Boolean read_only) :
		fp(fp), base_addr(base_addr), read_only(read_only)
	{
		int size;
		void* ptr;
		long fpos; /* position in the file for use by fread, fseek etc */
		int memaddr = 0;

		fseek(fp, 0L, SEEK_SET);

		/* read the size of BrokerState */
		fread(&size, sizeof(size), 1, fp);
		fpos = ftell(fp);
		printf("BrokerState size=%d\n", size);
		mem_list.Add(fpos, memaddr, size);
		fseek(fp, size, SEEK_CUR); /* skip the broker state structure */
		memaddr += size;

		/* read the Config filename buffer location */
		fpos = ftell(fp);
		fread(&ptr, sizeof(ptr), 1, fp);
		printf("config filename buffer addr=%p\n", ptr);
		mem_list.Add(fpos, memaddr, sizeof(ptr));
		memaddr += sizeof(ptr);

		/* read the size of Sockets */
		fread(&size, sizeof(size), 1, fp);
		fpos = ftell(fp);
		printf("socket buffer size=%d\n", size);
		mem_list.Add(fpos, memaddr, size);
		fseek(fp, size, SEEK_CUR); /* skip the socket structure */
		memaddr += size;
		
		/* read the size of the MQTTProtocol state */
		fread(&size, sizeof(size), 1, fp);
		fpos = ftell(fp);
		printf("MQTTProtocol state size=%d\n", size);
		mem_list.Add(fpos, memaddr, size);
		fseek(fp, size, SEEK_CUR); /* skip the MQTTProtocol structure */
		memaddr += size;

		/* Now read in the heap elements */
		while (!feof(fp))
		{
			fpos = ftell(fp) + sizeof(int*) + sizeof(int);
			if (fread(&memaddr, sizeof(int*), 1, fp) == 1)
			{
				if (fread(&size, sizeof(int), 1, fp) == 1)
				{
					if (size >= 0) // if the size is bad, ignore it
						mem_list.Add(fpos, memaddr, size);
					fseek(fp, size, SEEK_CUR);
				}
				else
				{
					/* this should not happen */
					printf("ERROR reading size at 0x%lX\n", ftell(fp));
					break;
				}
			}
			else
			{
				/* this could be caused by end of file, so check for that */
				if (ferror(fp))
					printf("ERROR reading address at 0x%lX feof %d\n", ftell(fp), feof(fp));
				break;
			}
		}
		//mem_list.Dump();
	}

	~BeMem()
	{
		fclose(fp);
	}

	Boolean read(BEMEMADDR64 addr, unsigned char& b)
	{
		long faddr = mem_list.GetFileAddr(addr + base_addr);
		if (faddr == -1)
			return FALSE;
		if (fseek(fp, faddr, SEEK_SET) != 0)
			return FALSE;
		return fread(&b, 1, 1, fp) == 1;
	}

	Boolean write(BEMEMADDR64 addr, unsigned char b)
	{
		if (read_only)
			return FALSE;
		printf("Warning: not expecting to WRITE!!!\n");
		addr -= base_addr;
		if (fseek(fp, addr, SEEK_SET) != 0)
			return FALSE;
		return fwrite(&b, 1, 1, fp) == 1;
	}
};


BEMEMEXPORT Boolean BEMEMENTRY bemem_read(void* ptr, BEMEMADDR32 addr, unsigned char & b)
{
	BeMem *bemem = (BeMem*)ptr;
	return bemem->read(addr, b);
}


BEMEMEXPORT Boolean BEMEMENTRY bemem_read_64(void* ptr, BEMEMADDR64 addr, unsigned char & b)
{
	BeMem *bemem = (BeMem*)ptr;
	return bemem->read(addr, b);
}


BEMEMEXPORT Boolean BEMEMENTRY bemem_write(void* ptr, BEMEMADDR32 addr, unsigned char b)
{
	BeMem *bemem = (BeMem*) ptr;
	return bemem->write(addr, b);
}


BEMEMEXPORT Boolean BEMEMENTRY bemem_write_64(void* ptr, BEMEMADDR64 addr, unsigned char b)
{
	BeMem *bemem = (BeMem*) ptr;
	return bemem->write(addr, b);
}


BEMEMEXPORT void * BEMEMENTRY bemem_create(const char *args, BEMEMADDR32 addr, const char *(&err))
{
	FILE *fp;
	BeMem *bemem;
	if ( !memcmp(args, "RO:", 3) )
	{
		if ( (fp = fopen(args+3, "rb")) == 0 )
		{
			err = "can't open file in read only mode";
			return 0;
		}
		bemem = new BeMem(fp, addr, TRUE);
	}
	else
	{
		if ( (fp = fopen(args, "rb+")) == 0 )
		{
			err = "can't open file in read/write mode";
			return 0;
		}
		bemem = new BeMem(fp, addr, FALSE);
	}
	if ( bemem == 0 )
	{
		fclose(fp);
		err = "out of memory";
		return 0;
	}
	return (void *) bemem;
}


BEMEMEXPORT void * BEMEMENTRY bemem_create_64(const char *args, BEMEMADDR64 addr, const char *(&err))
{
	FILE *fp;
	BeMem *bemem;
	if ( !memcmp(args, "RO:", 3) )
	{
		if ( (fp = fopen(args+3, "rb")) == 0 )
		{
			err = "can't open file in read only mode";
			return 0;
		}
		bemem = new BeMem(fp, addr, TRUE);
	}
	else
	{
		if ( (fp = fopen(args, "rb+")) == 0 )
		{
			err = "can't open file in read/write mode";
			return 0;
		}
		bemem = new BeMem(fp, addr, FALSE);
	}
	if ( bemem == 0 )
	{
		fclose(fp);
		err = "out of memory";
		return 0;
	}
	return (void *) bemem;
}

BEMEMEXPORT void BEMEMENTRY bemem_delete(void * ptr)
{
	BeMem *bemem = (BeMem *) ptr;
	delete bemem;
}

#ifdef DOS32

// Note: Required due to the way DOS CauseWay DLLs are constructed

int main(int term) { term=term; return 0; }

#endif

#ifdef AIX

// Note: This section is needed if using a BE executable that uses the
// old AIX specific loadAndInit call, rather the standard UNIX dlopen.

extern "C" {

BEMEM_EXPORT * __start(void)
	{
	static BEMEM_EXPORT exports[] =
		{
		(BEMEM_EP) bemem_read   , "bemem_read"   ,
		(BEMEM_EP) bemem_write  , "bemem_write"  ,
		(BEMEM_EP) bemem_create , "bemem_create" ,
		(BEMEM_EP) bemem_delete , "bemem_delete" ,
		(BEMEM_EP) 0            , 0
		};
	return exports;
	}

}

#endif

#ifdef NW

// Rather ugly mechanism required to make NLMs behave like DLLs under NetWare.

#include <conio.h>
#include <process.h>
#include <advanced.h>

static int tid;

extern "C" BEMEMEXPORT void BEMEMENTRY _bemem_term(void);

BEMEMEXPORT void BEMEMENTRY _bemem_term(void)
	{
	ResumeThread(tid);
	}

int main(int argc, char *argv[])
	{
	argc=argc; argv=argv; // Suppress warnings
	int nid = GetNLMID();
	SetAutoScreenDestructionMode(TRUE);
	SetNLMDontUnloadFlag(nid);
	tid = GetThreadID();
	SuspendThread(tid);
	ClearNLMDontUnloadFlag(nid);
	return 0;
	}

#endif

