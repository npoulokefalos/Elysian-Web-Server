/*
 * This file is part of Elysian Web Server
 *
 * Copyright (C) 2016,  Nikos Poulokefalos, npoulokefalos at gmail.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "elysian.h"
#include "elysian_port.h"

/* -----------------------------------------------------------------------------------------------------------
 RAM filesystem
----------------------------------------------------------------------------------------------------------- */

elysian_fs_ram_file_t fs_ram_files ={
    .name = NULL,
	.cbuf = NULL,
	.read_handles = 0,
	.write_handles = 0,
    .next = &fs_ram_files
};

#if 0
void elysian_fs_ram_init(){
	fs_ram_files.name = NULL;
	fs_ram_files.cbuf = NULL;
	fs_ram_files.read_handles = 0;
	fs_ram_files.write_handles = 0;
	fs_ram_files.next = &fs_ram_files;
}
#endif

elysian_err_t elysian_fs_ram_fopen(elysian_t* server, char* abs_path, elysian_file_mode_t mode, elysian_file_t* file){
	elysian_err_t err;
    elysian_fs_ram_file_t* fs_ram_file;

	ELYSIAN_LOG("Opening RAM file with name %s", abs_path);
    
	/*
	* Findout if file exists
	*/
	fs_ram_file = fs_ram_files.next;
	while(fs_ram_file != &fs_ram_files){
		if(strcmp(abs_path, fs_ram_file->name) == 0){
			break;
		}
		fs_ram_file = fs_ram_file->next;
	};
	
	if(mode == ELYSIAN_FILE_MODE_READ){
		
		if(fs_ram_file == &fs_ram_files){
			return ELYSIAN_ERR_NOTFOUND;
		}
		
		if(fs_ram_file->write_handles){
			/* File is opened for write, cannot read */
			return ELYSIAN_ERR_FATAL;
		}
		
		fs_ram_file->read_handles++;
		
		file->descriptor.ram.fd = fs_ram_file;
		file->descriptor.ram.pos = 0;
		file->mode = ELYSIAN_FILE_MODE_READ;
        
		return ELYSIAN_ERR_OK;
	}
	
	if(mode == ELYSIAN_FILE_MODE_WRITE){
		/*
		* If file exists, remove it first
		*/
		if(fs_ram_file != &fs_ram_files){
            ELYSIAN_LOG("Ram file exists, removing it");
			if(fs_ram_file->read_handles == 0 && fs_ram_file->write_handles == 0){
				err = elysian_fs_ram_fremove(server, abs_path);
				if(err != ELYSIAN_ERR_OK){
					return err;
				}	
			}else{
				/* File is opened for read/write, cannot remove */
                ELYSIAN_LOG("File is already opened..");
				return ELYSIAN_ERR_FATAL;
			}
		}
		
        ELYSIAN_LOG("allocating new ram file..");
		fs_ram_file = elysian_mem_malloc(server, sizeof(elysian_fs_ram_file_t), ELYSIAN_MEM_MALLOC_PRIO_NORMAL);
		if(!fs_ram_file){
			return ELYSIAN_ERR_POLL;
		}
		
		fs_ram_file->name = elysian_mem_malloc(server, strlen(abs_path) + 1, ELYSIAN_MEM_MALLOC_PRIO_NORMAL);
		if(!fs_ram_file->name){
			elysian_mem_free(server, fs_ram_file);
			return ELYSIAN_ERR_POLL;
		}
		strcpy(fs_ram_file->name, abs_path);
		fs_ram_file->cbuf = NULL;
		fs_ram_file->write_handles = 1;
		fs_ram_file->read_handles = 0;
		
		file->descriptor.ram.fd = fs_ram_file;
		file->descriptor.ram.pos = 0;
		file->mode = ELYSIAN_FILE_MODE_WRITE;
		
		/*
		** Add the file to the list
		*/
		fs_ram_file->next = fs_ram_files.next;
		fs_ram_files.next = fs_ram_file;

        ELYSIAN_LOG("Ram file created..");
        
		return ELYSIAN_ERR_OK;
	}

	return ELYSIAN_ERR_FATAL;
}

elysian_err_t elysian_fs_ram_fsize(elysian_t* server, elysian_file_t* file, uint32_t* filesize){
	elysian_cbuf_t* cbuf_next;
    elysian_fs_ram_file_t* fs_ram_file;
    elysian_file_ram_t* file_ram;
    
    file_ram = &file->descriptor.ram;
    fs_ram_file = file_ram->fd;
    
	*filesize = 0;
	cbuf_next =	fs_ram_file->cbuf;
	while(cbuf_next){
		*filesize = (*filesize) + (cbuf_next->len);
        cbuf_next = cbuf_next->next;
	}
    return ELYSIAN_ERR_OK;
}

elysian_err_t elysian_fs_ram_fseek(elysian_t* server, elysian_file_t* file, uint32_t seekpos){
    elysian_err_t err;
	uint32_t filesize;
    elysian_file_ram_t* file_ram;

    file_ram = &file->descriptor.ram;
	
	err = elysian_fs_ram_fsize(server, file, &filesize);
	if(err != ELYSIAN_ERR_OK){
		return err;
	}
	
	ELYSIAN_ASSERT(seekpos < filesize, "");
	
	if(seekpos > filesize){
		return ELYSIAN_ERR_FATAL;
	}
	
	file_ram->pos = seekpos;

    return err;
}

elysian_err_t elysian_fs_ram_ftell(elysian_t* server,  elysian_file_t* file, uint32_t* seekpos){
    elysian_file_ram_t* file_ram = &file->descriptor.ram;
	*seekpos = file_ram->pos;
	return ELYSIAN_ERR_OK;
}


int elysian_fs_ram_fread(elysian_t* server,  elysian_file_t* file, uint8_t* buf, uint32_t buf_size){
    elysian_fs_ram_file_t* fs_ram_file;
    elysian_file_ram_t* file_ram;
    uint32_t cpy_sz;
    uint32_t read_sz;
    uint32_t pos;
    elysian_cbuf_t* cbuf;
    
    file_ram = &file->descriptor.ram;
    fs_ram_file = file_ram->fd;
    
    cbuf = fs_ram_file->cbuf;
    pos = file_ram->pos;
    while(cbuf){
        if(pos < cbuf->len){break;}
        pos -= cbuf->len;
        cbuf = cbuf->next;
    }
    read_sz = 0;
    while(cbuf){
        if(read_sz == buf_size){break;}
        cpy_sz = (cbuf->len - pos > buf_size - read_sz) ? buf_size - read_sz : cbuf->len - pos;
        memcpy(&buf[read_sz], &cbuf->data[pos], cpy_sz);
        read_sz += cpy_sz;
        pos += cpy_sz;
        if(pos == cbuf->len){
            cbuf = cbuf->next;
            pos = 0;
        }
    }
    file_ram->pos += read_sz;
    return read_sz;
}


int elysian_fs_ram_fwrite(elysian_t* server, elysian_file_t* file, uint8_t* buf, uint32_t buf_size){
    elysian_fs_ram_file_t* fs_ram_file;
    elysian_file_ram_t* file_ram;
	elysian_cbuf_t* write_cbuf;
	
    file_ram = &file->descriptor.ram;
    fs_ram_file = file_ram->fd;
    
	write_cbuf = elysian_cbuf_alloc(server, buf, buf_size);
	if(!write_cbuf){
		return 0;
	}
	
    if(fs_ram_file->cbuf == NULL) {
        fs_ram_file->cbuf = write_cbuf;
    }else{
        elysian_cbuf_t* cbuf;
        cbuf = fs_ram_file->cbuf;
        while(cbuf->next){
            cbuf = cbuf->next;
        }
        cbuf->next = write_cbuf;
    }
    return buf_size;
}

elysian_err_t elysian_fs_ram_fclose(elysian_t* server, elysian_file_t* file){
    elysian_fs_ram_file_t* fs_ram_file;
    elysian_file_ram_t* file_ram;
	
    file_ram = &file->descriptor.ram;
    fs_ram_file = file_ram->fd;
    
	if(file->mode == ELYSIAN_FILE_MODE_READ){
		ELYSIAN_ASSERT(fs_ram_file->read_handles > 0, "");
		fs_ram_file->read_handles--;
        return ELYSIAN_ERR_OK;
	}
	
	if(file->mode == ELYSIAN_FILE_MODE_WRITE){
		ELYSIAN_ASSERT(fs_ram_file->write_handles > 0, "");
		fs_ram_file->write_handles--;
        return ELYSIAN_ERR_OK;
	}
    
    return ELYSIAN_ERR_FATAL;
}

elysian_err_t elysian_fs_ram_fremove(elysian_t* server, char* abs_path){
    elysian_fs_ram_file_t* fs_ram_file;
    elysian_fs_ram_file_t* fs_ram_file_prev;
	
    ELYSIAN_LOG("Removing ram file..%s",abs_path);
    
	/*
	** Locate the file
	*/
	fs_ram_file_prev = &fs_ram_files;
	fs_ram_file = fs_ram_files.next;
	while(fs_ram_file != &fs_ram_files){
		if(strcmp(abs_path, fs_ram_file->name) == 0){
			fs_ram_file_prev->next = fs_ram_file->next;
			break;
		}
		fs_ram_file_prev = fs_ram_file;
		fs_ram_file = fs_ram_file->next;
	};
	
	
	if(fs_ram_file == &fs_ram_files){
		/*
		** File not found
		*/
		ELYSIAN_ASSERT(0, "");
		return ELYSIAN_ERR_FATAL;
	}
	
	if(fs_ram_file->read_handles > 0 || fs_ram_file->write_handles > 0){
		return ELYSIAN_ERR_FATAL;
	}
	
    ELYSIAN_LOG("Removing cbufs of ram file..%s",abs_path);
    
	elysian_mem_free(server, fs_ram_file->name);
	elysian_cbuf_list_free(server, fs_ram_file->cbuf);
	elysian_mem_free(server, fs_ram_file);
    return ELYSIAN_ERR_OK;
}

/* -----------------------------------------------------------------------------------------------------------
 ROM filesystem
----------------------------------------------------------------------------------------------------------- */
elysian_err_t elysian_fs_rom_fopen(elysian_t* server, char* abs_path, elysian_file_mode_t mode, elysian_file_t* file){
	int i;
	
    ELYSIAN_LOG("Opening file <%s>..", abs_path);
    
	if(mode == ELYSIAN_FILE_MODE_WRITE){
		return ELYSIAN_ERR_FATAL;
	}
	
	for(i = 0; i < server->rom_fs_size; i++){
		printf("checking with %s --------- %s\r\n",  abs_path, server->rom_fs[i].name);
		if(strcmp(abs_path, server->rom_fs[i].name) == 0){
			(file)->descriptor.rom.ptr = server->rom_fs[i].ptr;
			(file)->descriptor.rom.size = server->rom_fs[i].size;
			(file)->descriptor.rom.pos = 0;
			return ELYSIAN_ERR_OK;
		}
	}
	
	return ELYSIAN_ERR_NOTFOUND;
}

elysian_err_t elysian_fs_rom_fsize(elysian_t* server, elysian_file_t* file, uint32_t* filesize){
    elysian_file_rom_t* file_rom = &file->descriptor.rom;
	*filesize = file_rom->size;
	return ELYSIAN_ERR_OK;
}


elysian_err_t elysian_fs_rom_fseek(elysian_t* server, elysian_file_t* file, uint32_t seekpos){
    elysian_file_rom_t* file_rom = &file->descriptor.rom;
	file_rom->pos = seekpos;
	return ELYSIAN_ERR_OK;
}

elysian_err_t elysian_fs_rom_ftell(elysian_t* server, elysian_file_t* file, uint32_t* seekpos){
    elysian_file_rom_t* file_rom = &file->descriptor.rom;
	*seekpos = file_rom->pos;
	return ELYSIAN_ERR_OK;
}


int elysian_fs_rom_fread(elysian_t* server, elysian_file_t* file, uint8_t* buf, uint32_t buf_size){
	uint32_t read_size;
    elysian_file_rom_t* file_rom = &file->descriptor.rom;
    
	read_size = (buf_size > file_rom->size - file_rom->pos) ? file_rom->size - file_rom->pos : buf_size;
	memcpy(buf, &file->descriptor.rom.ptr[file_rom->pos], read_size);
	file_rom->pos += read_size;
	return read_size;
}

int elysian_fs_rom_fwrite(elysian_t* server, elysian_file_t* file, uint8_t* buf, uint32_t buf_size){
	return -1;
}

elysian_err_t elysian_fs_rom_fclose(elysian_t* server, elysian_file_t* file){
	/*
	** No special handling is needed.
	*/
    return ELYSIAN_ERR_OK;
}

elysian_err_t elysian_fs_rom_fremove(elysian_t* server, char* abs_path){
	return ELYSIAN_ERR_OK;
}


/* -----------------------------------------------------------------------------------------------------------
 Web Server internal filesystem
----------------------------------------------------------------------------------------------------------- */
const uint8_t ELYSIAN_FS_HUGE_FILE_DATA[1];

elysian_err_t elysian_fs_ws_fopen(elysian_t* server, char* abs_path, elysian_file_mode_t mode, elysian_file_t* file){
    uint32_t i;
    char status_code_page[32];
    
    ELYSIAN_LOG("Opening file <%s>..", abs_path);
    
	if(mode == ELYSIAN_FILE_MODE_WRITE){
		return ELYSIAN_ERR_FATAL;
	}

	/*
	** Zero-sized file for messages without body
	*/
	if(strcmp(abs_path, ELYSIAN_FS_EMPTY_FILE_NAME) == 0){
		(file)->descriptor.rom.ptr = (uint8_t*) "";
		(file)->descriptor.rom.size = strlen((char*) (file)->descriptor.rom.ptr);
		(file)->descriptor.rom.pos = 0;
		return ELYSIAN_ERR_OK;
	}
	
	/*
	** Huge-sized file for testing
	*/
	if(strcmp(abs_path, ELYSIAN_FS_HUGE_FILE_NAME) == 0){
		(file)->descriptor.rom.ptr = (uint8_t*) ELYSIAN_FS_HUGE_FILE_DATA;
		(file)->descriptor.rom.size = 250 * 1024 * 1024;
		(file)->descriptor.rom.pos = 0;
		return ELYSIAN_ERR_OK;
	}
	
	/*
	** If this is a status code page, send the default user-friendly message
	*/
	for(i = 0; i < ELYSIAN_HTTP_STATUS_CODE_MAX; i++){
		sprintf(status_code_page, "/%u.html", elysian_http_get_status_code_num(i));
		if(strcmp(abs_path, status_code_page) == 0){
			(file)->descriptor.rom.ptr = (uint8_t*) elysian_http_get_status_code_body(i);
			(file)->descriptor.rom.size = strlen((char*) (file)->descriptor.rom.ptr);
			(file)->descriptor.rom.pos = 0;
			return ELYSIAN_ERR_OK;
		}
	}
	
	return ELYSIAN_ERR_NOTFOUND;
}

elysian_err_t elysian_fs_ws_fsize(elysian_t* server, elysian_file_t* file, uint32_t* filesize){
    elysian_file_rom_t* file_rom = &file->descriptor.rom;
	*filesize = file_rom->size;
	return ELYSIAN_ERR_OK;
}

elysian_err_t elysian_fs_ws_fseek(elysian_t* server, elysian_file_t* file, uint32_t seekpos){
    elysian_file_rom_t* file_rom = &file->descriptor.rom;
	file_rom->pos = seekpos;
	return ELYSIAN_ERR_OK;
}

elysian_err_t elysian_fs_ws_ftell(elysian_t* server, elysian_file_t* file, uint32_t* seekpos){
    elysian_file_rom_t* file_rom = &file->descriptor.rom;
	*seekpos = file_rom->pos;
	return ELYSIAN_ERR_OK;
}

int elysian_fs_ws_fread(elysian_t* server, elysian_file_t* file, uint8_t* buf, uint32_t buf_size){
	uint32_t read_size;
    elysian_file_rom_t* file_rom = &file->descriptor.rom;

	read_size = (buf_size > file_rom->size - file_rom->pos) ? file_rom->size - file_rom->pos : buf_size;
	
	if (file->descriptor.rom.ptr == (uint8_t*) ELYSIAN_FS_HUGE_FILE_DATA) {
		memset(buf, 0, read_size);
	} else {
		memcpy(buf, &file->descriptor.rom.ptr[file_rom->pos], read_size);
	}

	file_rom->pos += read_size;
	return read_size;
}

int elysian_fs_ws_fwrite(elysian_t* server, elysian_file_t* file, uint8_t* buf, uint32_t buf_size){
	ELYSIAN_ASSERT(0, "");
	return -1;
}

elysian_err_t elysian_fs_ws_fclose(elysian_t* server, elysian_file_t* file){
    return ELYSIAN_ERR_OK;
}

elysian_err_t elysian_fs_ws_fremove(elysian_t* server, char* abs_path){
	ELYSIAN_ASSERT(0, "");
	return ELYSIAN_ERR_FATAL;
}
