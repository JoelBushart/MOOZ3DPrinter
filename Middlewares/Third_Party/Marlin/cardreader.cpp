
#include "Marlin.h"
#include "cardreader.h"
#include "hmi.h"
#include "usbh_usr.h"
#if (!MB(STM_3DPRINT))
#include "ultralcd.h"
#include "stepper.h"
#include "temperature.h"
#include "language.h"
#endif


extern char USBPath[4];
#ifdef SDSTSUPPORT

CardReader::CardReader()
{
    uint32_t loop;

    filesize = 0;
    sdpos = 0;
    sdprinting = false;
    cardOK = false;
    rootIsOpened = false;
    saving = false;
    logging = false;
    workDirDepth = 0;
    file_subcall_ctr=0;
    cardReaderInitialized = false;

    curDir = NULL;
    diveDirName = NULL;
    filenameIsDir = false;
    lsAction = LS_SerialPrint;
    nrFiles = 0;

    memset(workDirParents, 0, sizeof(workDirParents));

    for (loop = 0; loop < SD_PROCEDURE_DEPTH; loop++) {
        fileOpened[loop] = 0;
    }

    memset(&fileSystem, 0, sizeof(FATFS));

    // Variable dedicated to autostart on SD
    autostart_stilltocheck=true; //the SD start is delayed, because otherwise the serial cannot answer fast enough to make contact with the host software.
    autostart_atmillis=0;

    lastnr=0;
    //power to SD reader
#if SDPOWER > -1
    SET_OUTPUT(SDPOWER);
    WRITE(SDPOWER,HIGH);
#endif //SDPOWER

    autostart_atmillis=millis()+5000;

    // Activate the SD card detection
    BSP_SD_DetectInit();
}


void CardReader::mountsd()
{
    cardOK = false;


    if (rootIsOpened == false) {
        if (BSP_SD_IsDetected() == 0) {
            SERIAL_ECHO_START;
            SERIAL_ECHOLNPGM(MSG_SD_INIT_FAIL);
            //SERIAL_ECHOLNPGM("CardReader::mountsd");  // BDI
        } else if ((FATFS_LinkDriver(&SD_Driver, SDPath) != FR_OK)||
                   (f_mount(&fileSystem, (TCHAR const*)SDPath, 0) != FR_OK)) {
            //FATFS_LinkDriver failed!
            SERIAL_ERROR_START;
            SERIAL_ERRORLNPGM(MSG_SD_VOL_INIT_FAIL);
        } else if (f_opendir(&root,SDPath) != FR_OK) {
            //f_opendir failed!
            SERIAL_ERROR_START;
            SERIAL_ERRORLNPGM(MSG_SD_OPENROOT_FAIL);
        } else {
            // SD card mounted!
            cardOK = true;
            rootIsOpened = true;
            cardReaderInitialized = true;  // BDI
            SERIAL_ECHO_START;
            SERIAL_ECHOLNPGM(MSG_SD_CARD_OK);
        }
    } else {
        cardOK = true;
    }

    workDir=root;
    curDir=&root;

}

char *starpos=NULL;

char *createFilename(char *buffer,FILINFO *pEntry) //buffer>12characters
{
    char *pos=buffer;

    for (uint8_t i = 0; i < 11; i++) {
        if (pEntry->fname[i] == ' ') {
            continue;
        }

        if (i == 8) {
            *pos++='.';
        }

        *pos++= pEntry->fname[i];
    }

    *pos++=0;
    return buffer;
}
uint16_t Filename_rank;
extern char selected_filename[40];
extern  int page;
void CardReader::lsDive(const char *prepend, DIR *parent, const char * const match/*=NULL*/)
{
    Filename_rank=0;
    FILINFO entry;
    uint8_t cnt=0;

#if _USE_LFN
    static char lfn[LONG_FILENAME_LENGTH];
    entry.lfname = lfn;
    entry.lfsize = sizeof(lfn);
#endif

    if ( !cardOK&&!USB_disk_status()) {
        //not Device!
        return;
    }

    while ((f_readdir(parent,&entry) == FR_OK)&&(entry.fname[0] != '\0')) {
        if ((entry.fattrib & AM_DIR) && (lsAction != LS_Count) && (lsAction != LS_GetFilename)) { // hence LS_SerialPrint
            continue;
            /*
            DIR subDir;
            char path[MAXPATHNAMELENGTH];
            char lfilename[FILENAME_LENGTH];
            createFilename(lfilename,&entry);

            path[0]=0;
            if(strlen(prepend)==0) //avoid leading / if already in prepend
            {
            strcat(path,"/");
            }
            strcat(path,prepend);
            strcat(path,lfilename);

            if (f_opendir(&subDir,path) != FR_OK)
            {
            if(lsAction==LS_SerialPrint)
            {
              SERIAL_ECHO_START;
              SERIAL_ECHOLN(MSG_SD_CANT_OPEN_SUBDIR);
              SERIAL_ECHOLN(path);
            }
            }
            else
            {
             continue;
            //strcat(path,"/");
            //lsDive(path,&subDir);
            //close done automatically by destructor of SdFile
            }*/
        } else {
            char fn0 = entry.fname[0];

            filenameIsDir=(entry.fattrib & AM_DIR) ;

            if (fn0 == 0X00) {
                break;    //DIR_NAME_FREE 0X00
            }

            if (fn0 == (char)0xE5 || fn0 == '.' || fn0 == '_') {
                continue;    //DIR_NAME_DELETED 0XE5
            }

            if (!filenameIsDir) {
                char *ptr = strchr(entry.fname, '.');

                if ((ptr==NULL) || (*(ptr+1)!='G')|| (*(ptr+2)!='C')|| (*(ptr+3)!='O')) {
                    continue;
                }
            }

            if (lsAction==LS_SerialPrint) {

                char pathAndFilename[LONG_FILENAME_LENGTH] = "\n";

                if (page==2) { //File_page
                    if (Filename_rank>=nextpage&&Filename_rank<=nextpage+5) {
                        HMISendCmd("t%d.txt=\"%s\"", Filename_rank-nextpage,entry.lfname);
                    }
                }

                strcat(pathAndFilename, prepend);
                strcat(pathAndFilename, entry.lfname);

                if (BSP_WifiIsFileCreation()) {
                    strcat(fileList, pathAndFilename);
                }

                if (selected_rank>0&&Filename_rank==selected_rank-1+nextpage) {
                    memset(selected_filename,0,sizeof(selected_filename));
                    strncpy(selected_filename, entry.lfname, int (strlen(pathAndFilename)-7));
                    starpos = (strchr(selected_filename,'*'));

                    if (starpos!=NULL) {
                        *(starpos)='\0';
                    }

                    p_card->openFile(selected_filename,true);
                }

                Filename_rank++;
            } else if (lsAction==LS_Count) {
                nrFiles++;
            } else if (lsAction==LS_GetFilename) {
                if (match != NULL) {
                    if (strcasecmp(match, entry.fname) == 0) {
                        return;
                    }
                } else if (cnt == nrFiles) {
                    return;
                }

                cnt++;
            }
        }
    }
}


bool CardReader::testPath( char *name, char **fname)
{
    DIR myDir;
    curDir=&root;
    char *dirname_start,*dirname_end;

//	if( !cardOK)
//		return false;

    if (name[0]=='/') {
        dirname_start=strchr(name,'/')+1;

        while (dirname_start>0) {
            dirname_end=strchr(dirname_start,'/');
            SERIAL_ECHO("start:");
            SERIAL_ECHOLN((int)(dirname_start-name));
            SERIAL_ECHO("end  :");
            SERIAL_ECHOLN((int)(dirname_end-name));

            if (dirname_end>0 && dirname_end>dirname_start) {
                char subdirname[13];
                strncpy(subdirname, dirname_start, dirname_end-dirname_start);
                subdirname[dirname_end-dirname_start]=0;
                SERIAL_ECHOLN(subdirname);

                if (f_opendir(&myDir,subdirname) != FR_OK) {
                    SERIAL_PROTOCOLPGM("open failed, File: ");
                    SERIAL_PROTOCOL(subdirname);
                    SERIAL_PROTOCOLLNPGM(".");
                    return false;
                } else {
                    SERIAL_ECHOLN("dive ok");
                }

                curDir=&myDir;
                dirname_start=dirname_end+1;
            } else { // the reminder after all /fsa/fdsa/ is the filename
                *fname=dirname_start;
                //SERIAL_ECHOLN("remaider");
                //SERIAL_ECHOLN(fname);
                break;
            }
        }
    } else { //relative path
        curDir=&workDir;
        *fname = name;
    }

    return true;
}


void CardReader::ls()
{
    uint32_t usb_status = USB_disk_status();

    if ( cardOK==0&&USB_disk_status()==0) {
        //No device
        return;
    }

    lsAction=LS_SerialPrint;
    fileList[0]='\0';

    if (sdorusb == 0 && cardOK == 1) {
        //List SD card file and SD card is OK
        f_opendir(&root,SDPath);
    } else if (sdorusb == 1 && USB_disk_status() == 1) {
        //List USB disk file and USB disk is OK
        f_opendir(&root,USBPath);
    } else {
        //No suitable device
        return;
    }

    f_readdir(&root,0);
    lsDive("",&root);

    if (BSP_WifiIsFileCreation()) {
        strcat(fileList,"\n");
        BSP_WifiParseTxBytes(fileList, strlen(fileList), BSP_WIFI_SOURCE_IS_PLATFORM);
    }

//    process_commands();

}



void CardReader::setroot()
{
    /*if(!workDir.openRoot(&volume))
    {
      SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
    }*/
    workDir=root;
    curDir=&root;
}

void CardReader::release()
{
    sdprinting = false;
    cardOK = false;
    cardReaderInitialized = false;
    rootIsOpened = false;

    FATFS_UnLinkDriver(SDPath);
    //SD card released!

    SERIAL_ECHO_START;
    SERIAL_ECHOLNPGM(MSG_SD_INIT_FAIL);
    SERIAL_ECHOLNPGM("CardReader::release"); // BDI
}

void CardReader::startFileprint()
{
//  if(cardOK||USB_disk_status()==1)
//  {
    sdprinting = true;
//  }
}

void CardReader::pauseSDPrint()
{
    if (sdprinting) {
        sdprinting = false;
    }
}


void CardReader::openLogFile(char* name)
{
    logging = true;
    openFile(name, false);
}

void CardReader::getAbsFilename(char *t)
{
#if 0
    FILINFO entry;
    uint8_t cnt=0;
    *t='/';
    t++;
    cnt++;

    /*
    for(uint8_t i=0;i<workDirDepth;i++)
    {
      FILINFO entry;
      get_fileinfo(workDirParents[i], &entry);
      strncpy(t, entry.fname, strlen(entry.fname));

      while(*t!=0 && cnt< MAXPATHNAMELENGTH)
      {t++;cnt++;}  //crawl counter forward.
    }
    */
    if (f_getcwd (t, MAXPATHNAMELENGTH) == FR_OK) {
        cnt = strlen(t);
        get_fileinfo(file, &entry);

        if (cnt < MAXPATHNAMELENGTH-13) {
            strcat(t, entry.fname);
        }

        t[0]=0;
    } else {
        t[0]=0;
    }

#endif
    strncpy( t, longFilename, strlen(longFilename));
}

void CardReader::openFile(char* name,bool read, bool replace_current/*=true*/)
{
//
    //if(!cardOK)
//
    //  return;

    // Test if a file or subfile (file_subcall_ctrl>0) is already opened.
    // In this case, two choices:
    //    - In case replace_current=true, the current file is closed and file_subcall_ctr is not incremented.
    //    - In case replace_current=false, the position is saved, the current file is closed and file_subcall_ctr is incremented.
    if (fileOpened[file_subcall_ctr]) {
        if (!replace_current) {
            if ((int)file_subcall_ctr>(int)SD_PROCEDURE_DEPTH-1) {
                SERIAL_ERROR_START;
                SERIAL_ERRORPGM("trying to call sub-gcode files with too many levels. MAX level is:");
                SERIAL_ERRORLN(SD_PROCEDURE_DEPTH);
                kill();
                return;
            }

            SERIAL_ECHO_START;
            SERIAL_ECHOPGM("SUBROUTINE CALL target:\"");
            SERIAL_ECHO(name);
            SERIAL_ECHOPGM("\" parent:\"");

            //store current filename and position
            getAbsFilename(filenames[file_subcall_ctr]);

            SERIAL_ECHO(filenames[file_subcall_ctr]);
            SERIAL_ECHOPGM("\" pos");
            SERIAL_ECHOLN(sdpos);
            filespos[file_subcall_ctr]=sdpos;
            fileOpened[file_subcall_ctr] = 0;
            file_subcall_ctr++;
        } else {
            SERIAL_ECHO_START;
            SERIAL_ECHOPGM("Now doing file: ");
            SERIAL_ECHOLN(name);
            fileOpened[file_subcall_ctr] = 0;
        }

    }

    //
    // Opening fresh file
    //
    else {
        file_subcall_ctr=0; //resetting procedure depth in case user cancels print while in procedure
        SERIAL_ECHO_START;
        SERIAL_ECHOPGM("Now fresh file: ");
        SERIAL_ECHOLN(name);
    }

    sdprinting = false;

#if 0
    DIR myDir;
    curDir=&root;
    char *fname=name;
    char *dirname_start,*dirname_end;

    if (name[0]=='/') { // Case of absolute path
        dirname_start=strchr(name,'/')+1;

        while (dirname_start>0) {
            dirname_end=strchr(dirname_start,'/');

            //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start-name));
            //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end-name));
            if (dirname_end>0 && dirname_end>dirname_start) {
                char subdirname[13];
                strncpy(subdirname, dirname_start, dirname_end-dirname_start);
                subdirname[dirname_end-dirname_start]=0;
                SERIAL_ECHOLN(subdirname);

                if (f_opendir(&myDir,subdirname) != FR_OK) {
                    SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
                    SERIAL_PROTOCOL(subdirname);
                    SERIAL_PROTOCOLLNPGM(".");
                    return;
                } else {
                    //SERIAL_ECHOLN("dive ok");
                }

                curDir=&myDir;
                dirname_start=dirname_end+1;
            } else { // the reminder after all /fsa/fdsa/ is the filename
                fname=dirname_start;
                //SERIAL_ECHOLN("remaider");
                //SERIAL_ECHOLN(fname);
                break;
            }

        }
    } else { //relative path
        curDir=&workDir;
    }

#endif

    char *fname=name;


    if ( !testPath(name, &fname)) {
        return;
    }

    strncpy(longFilename, fname, strlen(fname));

    if (read) {

        char nameBuffer[64];

        if (sdorusb) {
            sprintf(nameBuffer, "%s%s.gcode", USBPath, name);
        } else {
            sprintf(nameBuffer, "%s%s.gcode", SDPath, name);
        }

#if 0

        // SERIAL_PROTOCOLLN(f_open(&file, name, FA_OPEN_EXISTING | FA_READ));
        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_NO_FILE)||
            printf("FA_ERR1\n");

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_NO_PATH) {
            printf("FA_ERR2\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_INVALID_NAME) {
            printf("FA_ERR3\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_INVALID_DRIVE) {
            printf("FA_ERR4\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_EXIST) {
            printf("FA_ERR5\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_DENIED) {
            printf("FA_ERR6\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_NOT_READY) {
            printf("FA_ERR7\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_DISK_ERR) {
            printf("FA_ERR8\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_INT_ERR) {
            printf("FA_ERR9\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_NO_FILESYSTEM) {
            printf("FA_ERR10\n");
        }

        if (f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ) == FR_OK) {
            fileOpened[file_subcall_ctr] = 1;
            filesize = file.fsize;
            SERIAL_PROTOCOLPGM(MSG_SD_FILE_OPENED);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLPGM(MSG_SD_SIZE);
            SERIAL_PROTOCOLLN(filesize);
            sdpos = 0;

            SERIAL_PROTOCOLLNPGM(MSG_SD_FILE_SELECTED);
            getfilename(0, fname);
            //lcd_setstatus(longFilename[0] ? longFilename : fname);
        } else {
            SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLLNPGM(".");
        }

#else
        FRESULT res = f_open(&file, nameBuffer, FA_OPEN_EXISTING | FA_READ);

        if (res == FR_OK) {
            fileOpened[file_subcall_ctr] = 1;
            filesize = file.fsize;
            SERIAL_PROTOCOLPGM(MSG_SD_FILE_OPENED);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLPGM(MSG_SD_SIZE);
            SERIAL_PROTOCOLLN(filesize);
            sdpos = 0;

            SERIAL_PROTOCOLLNPGM(MSG_SD_FILE_SELECTED);
            getfilename(0, fname);
        } else {
            SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLLNPGM(".f1");
            //FA_ERR
        }

#endif
    } else {
        //write
        if (f_open(&file, fname, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
            SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLLNPGM(".");
        } else {
            saving = true;
            SERIAL_PROTOCOLPGM(MSG_SD_WRITE_TO_FILE);
            SERIAL_PROTOCOLLN(name);
            lcd_setstatus(fname);
        }
    }

}

void CardReader::removeFile(char* name)
{
    char *fname=name;

    if (!cardOK) {
        return;
    }

    if ( !testPath( name, &fname)) {
        return;
    }

    if ( !strncmp(name, filename, strlen(name))) {
        fileOpened[file_subcall_ctr] = 0;
        f_close(&file);
        sdprinting = false;
    }

    if ( f_unlink (fname) == FR_OK) {
        SERIAL_PROTOCOLPGM("File deleted:");
        SERIAL_PROTOCOLLN(fname);
        sdpos = 0;
    } else {
        SERIAL_PROTOCOLPGM("Deletion failed, File: ");
        SERIAL_PROTOCOL(fname);
        SERIAL_PROTOCOLLNPGM(".");
    }

}

void CardReader::getStatus()
{

    if (sdpos>0) {
        HMISendCmd("j0.val=%d" ,(100*(uint32_t)sdpos/filesize));
        HMISendCmd("t1.txt=\"%d%%\"" ,(100*(uint32_t)sdpos/filesize));

        if (sdpos>=filesize) {
            HMISendCmd("sys0=3");
        }
    }
}


void CardReader::write_command(char *buf)
{
    unsigned int lastBufferEntry;
    FRESULT writeStatus;
    char* begin = buf;
    char* npos = 0;
    char* end = buf + strlen(buf) - 1;

    if ((npos = strchr(buf, 'N')) != NULL) {
        begin = strchr(npos, ' ') + 1;
        end = strchr(npos, '*') - 1;
    }

    end[1] = '\r';
    end[2] = '\n';

    writeStatus = f_write(&file, begin, &(end[2]) - begin + 1, &lastBufferEntry);

    if ( 	(writeStatus != FR_OK) ||
            (lastBufferEntry != (unsigned int)(&(end[2]) - begin + 1))) {
        SERIAL_ERROR_START;
        SERIAL_ERRORLNPGM(MSG_SD_ERR_WRITE_TO_FILE);
    }
}


void CardReader::checkautostart(bool force)
{
    if (!force) {
        if (!autostart_stilltocheck) {
            return;
        }

        if (autostart_atmillis>millis()) {
            return;
        }
    }

    autostart_stilltocheck=false;

    if (!cardOK) {
        mountsd();

        if (!cardOK) { //fail
            return;
        }
    }

#if 0
    // File write test
    FIL f;
    f_open(&f, "0:/test.txt", FA_CREATE_ALWAYS | FA_WRITE);
    const char *buffer = "1rtqdcfh4wrt4fbfgfah5eyht";
    UINT bw = 0;
    f_write(&f, buffer, strlen(buffer), &bw);
    f_close(&f);
#endif
    char autoname[30];
    sprintf(autoname, PSTR("auto%i.g"), lastnr);

    for (int8_t i=0; i<(int8_t)strlen(autoname); i++) {
        autoname[i]=tolower(autoname[i]);
    }

    bool found=false;

    if (f_readdir(&root, 0) == FR_OK) {  /* Rewind directory object */
        FILINFO entry;

        while ((f_readdir(&root,&entry) == FR_OK)&&(entry.fname[0] != '\0')) {
            for (int8_t i=0; i<(int8_t)strlen((char*)entry.fname); i++) {
                entry.fname[i]=tolower(entry.fname[i]);
            }

            //Serial.print((char*)p.name);
            //Serial.print(" ");
            //Serial.println(autoname);
            if (entry.fname[9]!='~') //skip safety copies
                if (strncmp((char*)entry.fname,autoname,5)==0) {
                    char cmd[30];

                    sprintf(cmd, PSTR("M23 %s"), autoname);
                    enquecommand(cmd);
                    Start_SD_Printf();
                    found=true;
                }
        }
    }

    if (!found) {
        lastnr=-1;
    } else {
        lastnr++;
    }
}

void CardReader::closefile(bool store_location)
{
    f_sync(&file);
    fileOpened[file_subcall_ctr] = 0;
    saving = false;
    logging = false;

    if (store_location) {
        //future: store printer state, filename and position for continuing a stopped print
        // so one can unplug the printer and continue printing the next day.

    }


}

void CardReader::getfilename(uint16_t nr, const char * const match/*=NULL*/)
{
    curDir=&workDir;
    lsAction=LS_GetFilename;
    nrFiles=nr;
    f_readdir(curDir, 0); // rewind current directory
    lsDive("",curDir,match);
}

uint16_t CardReader::getnrfilenames()
{
    curDir=&workDir;
    lsAction=LS_Count;
    nrFiles=0;
    f_readdir(curDir, 0); // rewind current directory
    lsDive("",curDir);
    //SERIAL_ECHOLN(nrFiles);
    return nrFiles;
}

void CardReader::chdir(const char * relpath)
{
    DIR newDir;

    if (!cardOK) {
        return;
    }

    if (f_opendir(&newDir,relpath) != FR_OK) {
        SERIAL_ECHO_START;
        SERIAL_ECHOPGM(MSG_SD_CANT_ENTER_SUBDIR);
        SERIAL_ECHOLN(relpath);
        return;
    } else {
        if (workDirDepth < MAX_DIR_DEPTH) {
            for (int d = ++workDirDepth; d--;) {
                workDirParents[d+1] = workDirParents[d];
            }

            workDirParents[0]=*curDir;
        }

        workDir = newDir;
        curDir = &workDir;
    }
}

void CardReader::updir()
{
    if (workDirDepth > 0) {
        --workDirDepth;
        workDir = workDirParents[0];

        for (int d = 0; d < workDirDepth; d++) {
            workDirParents[d] = workDirParents[d+1];
        }
    }
}


void CardReader::printingHasFinished()
{
    st_synchronize();
    {
        quickStop();
        fileOpened[file_subcall_ctr] = 0;
        sdprinting = false;

        if (SD_FINISHED_STEPPERRELEASE) {
            finishAndDisableSteppers();
            enquecommand_P(PSTR(SD_FINISHED_RELEASECOMMAND));
        }

        autotempShutdown();
#if defined (SD_SETTINGS)

        if (strcasecmp(p_card->longFilename, CONFIG_FILE_NAME)==0) {
            printSettings();
        }

#endif
    }
}




#endif //SDSTSUPPORT

#ifdef SDSUPPORT



CardReader::CardReader()
{
    filesize = 0;
    sdpos = 0;
    sdprinting = false;
    cardOK = false;
    saving = false;
    logging = false;
    autostart_atmillis=0;
    workDirDepth = 0;
    file_subcall_ctr=0;
    memset(workDirParents, 0, sizeof(workDirParents));

    autostart_stilltocheck=true; //the SD start is delayed, because otherwise the serial cannot answer fast enough to make contact with the host software.
    lastnr=0;
    //power to SD reader
#if SDPOWER > -1
    SET_OUTPUT(SDPOWER);
    WRITE(SDPOWER,HIGH);
#endif //SDPOWER

    autostart_atmillis=millis()+5000;
}

char *createFilename(char *buffer,const dir_t &p) //buffer>12characters
{
    char *pos=buffer;

    for (uint8_t i = 0; i < 11; i++) {
        if (p.name[i] == ' ') {
            continue;
        }

        if (i == 8) {
            *pos++='.';
        }

        *pos++=p.name[i];
    }

    *pos++=0;
    return buffer;
}


void CardReader::lsDive(const char *prepend, SdFile parent, const char * const match/*=NULL*/)
{
    dir_t p;
    uint8_t cnt=0;

    while (parent.readDir(p, longFilename) > 0) {
        if ( DIR_IS_SUBDIR(&p) && lsAction!=LS_Count && lsAction!=LS_GetFilename) { // hence LS_SerialPrint

            char path[13*2];
            char lfilename[13];
            createFilename(lfilename,p);

            path[0]=0;

            if (strlen(prepend)==0) { //avoid leading / if already in prepend
                strcat(path,"/");
            }

            strcat(path,prepend);
            strcat(path,lfilename);
            strcat(path,"/");

            //Serial.print(path);

            SdFile dir;

            if (!dir.open(parent,lfilename, O_READ)) {
                if (lsAction==LS_SerialPrint) {
                    SERIAL_ECHO_START;
                    SERIAL_ECHOLN(MSG_SD_CANT_OPEN_SUBDIR);
                    SERIAL_ECHOLN(lfilename);
                }
            }

            lsDive(path,dir);
            //close done automatically by destructor of SdFile


        } else {
            char pn0 = p.name[0];

            if (pn0 == DIR_NAME_FREE) {
                break;
            }

            if (pn0 == DIR_NAME_DELETED || pn0 == '.' || pn0 == '_') {
                continue;
            }

            char lf0 = longFilename[0];

            if (lf0 == '.' || lf0 == '_') {
                continue;
            }

            if (!DIR_IS_FILE_OR_SUBDIR(&p)) {
                continue;
            }

            filenameIsDir=DIR_IS_SUBDIR(&p);


            if (!filenameIsDir) {
                if (p.name[8]!='G') {
                    continue;
                }

                if (p.name[9]=='~') {
                    continue;
                }
            }

            //if(cnt++!=nr) continue;
            createFilename(filename,p);

            if (lsAction==LS_SerialPrint) {
                SERIAL_PROTOCOL(prepend);
                SERIAL_PROTOCOLLN(filename);
            } else if (lsAction==LS_Count) {
                nrFiles++;
            } else if (lsAction==LS_GetFilename) {
                if (match != NULL) {
                    if (strcasecmp(match, filename) == 0) {
                        return;
                    }
                } else if (cnt == nrFiles) {
                    return;
                }

                cnt++;

            }
        }
    }
}

void CardReader::ls()
{
    lsAction=LS_SerialPrint;

    if (lsAction==LS_Count) {
        nrFiles=0;
    }

    root.rewind();
    lsDive("",root);
}


void CardReader::initsd()
{
    cardOK = false;

    if (root.isOpen()) {
        root.close();
    }

#ifdef SDSLOW

    if (!card.init(SPI_HALF_SPEED,SDSS)
#if defined(LCD_SDSS) && (LCD_SDSS != SDSS)
        && !card.init(SPI_HALF_SPEED,LCD_SDSS)
#endif
       )
#else
    if (!card.init(SPI_FULL_SPEED,SDSS)
#if defined(LCD_SDSS) && (LCD_SDSS != SDSS)
        && !card.init(SPI_FULL_SPEED,LCD_SDSS)
#endif
       )
#endif
    {
        //if (!card.init(SPI_HALF_SPEED,SDSS))
        SERIAL_ECHO_START;
        SERIAL_ECHOLNPGM(MSG_SD_INIT_FAIL);
    } else if (!volume.init(&card)) {
        SERIAL_ERROR_START;
        SERIAL_ERRORLNPGM(MSG_SD_VOL_INIT_FAIL);
    } else if (!root.openRoot(&volume)) {
        SERIAL_ERROR_START;
        SERIAL_ERRORLNPGM(MSG_SD_OPENROOT_FAIL);
    } else {
        cardOK = true;
        SERIAL_ECHO_START;
        SERIAL_ECHOLNPGM(MSG_SD_CARD_OK);
    }

    workDir=root;
    curDir=&root;
    /*
    if(!workDir.openRoot(&volume))
    {
      SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
    }
    */

}

void CardReader::setroot()
{
    /*if(!workDir.openRoot(&volume))
    {
      SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
    }*/
    workDir=root;

    curDir=&workDir;
}
void CardReader::release()
{
    sdprinting = false;
    cardOK = false;
}

void CardReader::startFileprint()
{
    if (cardOK) {
        sdprinting = true;
    }
}

void CardReader::pauseSDPrint()
{
    if (sdprinting) {
        sdprinting = false;
    }
}


void CardReader::openLogFile(char* name)
{
    logging = true;
    openFile(name, false);
}

void CardReader::getAbsFilename(char *t)
{
    uint8_t cnt=0;
    *t='/';
    t++;
    cnt++;

    for (uint8_t i=0; i<workDirDepth; i++) {
        workDirParents[i].getFilename(t); //SDBaseFile.getfilename!

        while (*t!=0 && cnt< MAXPATHNAMELENGTH) {
            t++;    //crawl counter forward.
            cnt++;
        }
    }

    if (cnt<MAXPATHNAMELENGTH-13) {
        file.getFilename(t);
    } else {
        t[0]=0;
    }
}

void CardReader::openFile(char* name,bool read, bool replace_current/*=true*/)
{
    if (!cardOK) {
        return;
    }

    if (file.isOpen()) { //replacing current file by new file, or subfile call
        if (!replace_current) {
            if ((int)file_subcall_ctr>(int)SD_PROCEDURE_DEPTH-1) {
                SERIAL_ERROR_START;
                SERIAL_ERRORPGM("trying to call sub-gcode files with too many levels. MAX level is:");
                SERIAL_ERRORLN(SD_PROCEDURE_DEPTH);
                kill();
                return;
            }

            SERIAL_ECHO_START;
            SERIAL_ECHOPGM("SUBROUTINE CALL target:\"");
            SERIAL_ECHO(name);
            SERIAL_ECHOPGM("\" parent:\"");

            //store current filename and position
            getAbsFilename(filenames[file_subcall_ctr]);

            SERIAL_ECHO(filenames[file_subcall_ctr]);
            SERIAL_ECHOPGM("\" pos");
            SERIAL_ECHOLN(sdpos);
            filespos[file_subcall_ctr]=sdpos;
            file_subcall_ctr++;
        } else {
            SERIAL_ECHO_START;
            SERIAL_ECHOPGM("Now doing file: ");
            SERIAL_ECHOLN(name);
        }

        file.close();
    } else { //opening fresh file
        file_subcall_ctr=0; //resetting procedure depth in case user cancels print while in procedure
        SERIAL_ECHO_START;
        SERIAL_ECHOPGM("Now fresh file: ");
        SERIAL_ECHOLN(name);
    }

    sdprinting = false;


    SdFile myDir;
    curDir=&root;
    char *fname=name;

    char *dirname_start,*dirname_end;

    if (name[0]=='/') {
        dirname_start=strchr(name,'/')+1;

        while (dirname_start>0) {
            dirname_end=strchr(dirname_start,'/');

            //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start-name));
            //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end-name));
            if (dirname_end>0 && dirname_end>dirname_start) {
                char subdirname[13];
                strncpy(subdirname, dirname_start, dirname_end-dirname_start);
                subdirname[dirname_end-dirname_start]=0;
                SERIAL_ECHOLN(subdirname);

                if (!myDir.open(curDir,subdirname,O_READ)) {
                    SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
                    SERIAL_PROTOCOL(subdirname);
                    SERIAL_PROTOCOLLNPGM(".");
                    return;
                } else {
                    //SERIAL_ECHOLN("dive ok");
                }

                curDir=&myDir;
                dirname_start=dirname_end+1;
            } else { // the reminder after all /fsa/fdsa/ is the filename
                fname=dirname_start;
                //SERIAL_ECHOLN("remaider");
                //SERIAL_ECHOLN(fname);
                break;
            }

        }
    } else { //relative path
        curDir=&workDir;
    }

    if (read) {
        if (file.open(curDir, fname, O_READ)) {
            filesize = file.fileSize();
            SERIAL_PROTOCOLPGM(MSG_SD_FILE_OPENED);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLPGM(MSG_SD_SIZE);
            SERIAL_PROTOCOLLN(filesize);
            sdpos = 0;

            SERIAL_PROTOCOLLNPGM(MSG_SD_FILE_SELECTED);
            getfilename(0, fname);
            lcd_setstatus(longFilename[0] ? longFilename : fname);
        } else {
            SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLLNPGM(".");
        }
    } else {
        //write
        if (!file.open(curDir, fname, O_CREAT | O_APPEND | O_WRITE | O_TRUNC)) {
            SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
            SERIAL_PROTOCOL(fname);
            SERIAL_PROTOCOLLNPGM(".");
        } else {
            saving = true;
            SERIAL_PROTOCOLPGM(MSG_SD_WRITE_TO_FILE);
            SERIAL_PROTOCOLLN(name);
            lcd_setstatus(fname);
        }
    }

}

void CardReader::removeFile(char* name)
{
    if (!cardOK) {
        return;
    }

    file.close();
    sdprinting = false;


    SdFile myDir;
    curDir=&root;
    char *fname=name;

    char *dirname_start,*dirname_end;

    if (name[0]=='/') {
        dirname_start=strchr(name,'/')+1;

        while (dirname_start>0) {
            dirname_end=strchr(dirname_start,'/');

            //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start-name));
            //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end-name));
            if (dirname_end>0 && dirname_end>dirname_start) {
                char subdirname[13];
                strncpy(subdirname, dirname_start, dirname_end-dirname_start);
                subdirname[dirname_end-dirname_start]=0;
                SERIAL_ECHOLN(subdirname);

                if (!myDir.open(curDir,subdirname,O_READ)) {
                    SERIAL_PROTOCOLPGM("open failed, File: ");
                    SERIAL_PROTOCOL(subdirname);
                    SERIAL_PROTOCOLLNPGM(".");
                    return;
                } else {
                    //SERIAL_ECHOLN("dive ok");
                }

                curDir=&myDir;
                dirname_start=dirname_end+1;
            } else { // the reminder after all /fsa/fdsa/ is the filename
                fname=dirname_start;
                //SERIAL_ECHOLN("remaider");
                //SERIAL_ECHOLN(fname);
                break;
            }

        }
    } else { //relative path
        curDir=&workDir;
    }

    if (file.remove(curDir, fname)) {
        SERIAL_PROTOCOLPGM("File deleted:");
        SERIAL_PROTOCOLLN(fname);
        sdpos = 0;
    } else {
        SERIAL_PROTOCOLPGM("Deletion failed, File: ");
        SERIAL_PROTOCOL(fname);
        SERIAL_PROTOCOLLNPGM(".");
    }

}

void CardReader::getStatus()
{
    if (cardOK) {
        SERIAL_PROTOCOLPGM(MSG_SD_PRINTING_BYTE);
        SERIAL_PROTOCOL(sdpos);
        SERIAL_PROTOCOLPGM("/");
        SERIAL_PROTOCOLLN(filesize);
    } else {
        SERIAL_PROTOCOLLNPGM(MSG_SD_NOT_PRINTING);
    }
}
void CardReader::write_command(char *buf)
{
    char* begin = buf;
    char* npos = 0;
    char* end = buf + strlen(buf) - 1;

    file.writeError = false;

    if ((npos = strchr(buf, 'N')) != NULL) {
        begin = strchr(npos, ' ') + 1;
        end = strchr(npos, '*') - 1;
    }

    end[1] = '\r';
    end[2] = '\n';
    end[3] = '\0';
    file.write(begin);

    if (file.writeError) {
        SERIAL_ERROR_START;
        SERIAL_ERRORLNPGM(MSG_SD_ERR_WRITE_TO_FILE);
    }
}


void CardReader::checkautostart(bool force)
{
    if (!force) {
        if (!autostart_stilltocheck) {
            return;
        }

        if (autostart_atmillis<millis()) {
            return;
        }
    }

    autostart_stilltocheck=false;

    if (!cardOK) {
        initsd();

        if (!cardOK) { //fail
            return;
        }
    }

    char autoname[30];
    sprintf_P(autoname, PSTR("auto%i.g"), lastnr);

    for (int8_t i=0; i<(int8_t)strlen(autoname); i++) {
        autoname[i]=tolower(autoname[i]);
    }

    dir_t p;

    root.rewind();

    bool found=false;

    while (root.readDir(p, NULL) > 0) {
        for (int8_t i=0; i<(int8_t)strlen((char*)p.name); i++) {
            p.name[i]=tolower(p.name[i]);
        }

        //Serial.print((char*)p.name);
        //Serial.print(" ");
        //Serial.println(autoname);
        if (p.name[9]!='~') //skip safety copies
            if (strncmp((char*)p.name,autoname,5)==0) {
                char cmd[30];

                sprintf_P(cmd, PSTR("M23 %s"), autoname);
                enquecommand(cmd);
                enquecommand_P(PSTR("M24"));
                found=true;
            }
    }

    if (!found) {
        lastnr=-1;
    } else {
        lastnr++;
    }
}

void CardReader::closefile(bool store_location)
{
    file.sync();
    file.close();
    saving = false;
    logging = false;

    if (store_location) {
        //future: store printer state, filename and position for continuing a stopped print
        // so one can unplug the printer and continue printing the next day.

    }


}

void CardReader::getfilename(uint16_t nr, const char * const match/*=NULL*/)
{
    curDir=&workDir;
    lsAction=LS_GetFilename;
    nrFiles=nr;
    curDir->rewind();
    lsDive("",*curDir,match);

}

uint16_t CardReader::getnrfilenames()
{
    curDir=&workDir;
    lsAction=LS_Count;
    nrFiles=0;
    curDir->rewind();
    lsDive("",*curDir);
    //SERIAL_ECHOLN(nrFiles);
    return nrFiles;
}

void CardReader::chdir(const char * relpath)
{
    SdFile newfile;
    SdFile *parent=&root;

    if (workDir.isOpen()) {
        parent=&workDir;
    }

    if (!newfile.open(*parent,relpath, O_READ)) {
        SERIAL_ECHO_START;
        SERIAL_ECHOPGM(MSG_SD_CANT_ENTER_SUBDIR);
        SERIAL_ECHOLN(relpath);
    } else {
        if (workDirDepth < MAX_DIR_DEPTH) {
            for (int d = ++workDirDepth; d--;) {
                workDirParents[d+1] = workDirParents[d];
            }

            workDirParents[0]=*parent;
        }

        workDir=newfile;
    }
}

void CardReader::updir()
{
    if (workDirDepth > 0) {
        --workDirDepth;
        workDir = workDirParents[0];
        int d;

        for (int d = 0; d < workDirDepth; d++) {
            workDirParents[d] = workDirParents[d+1];
        }
    }
}


void CardReader::printingHasFinished()
{
    st_synchronize();

    if (file_subcall_ctr>0) { //heading up to a parent file that called current as a procedure.
        file.close();
        file_subcall_ctr--;
        openFile(filenames[file_subcall_ctr],true,true);
        setIndex(filespos[file_subcall_ctr]);
        startFileprint();
    } else {
        quickStop();
        file.close();
        sdprinting = false;

        if (SD_FINISHED_STEPPERRELEASE) {
            //finishAndDisableSteppers();
            enquecommand_P(PSTR(SD_FINISHED_RELEASECOMMAND));
        }

        autotempShutdown();
    }
}
#endif //SDSUPPORT
