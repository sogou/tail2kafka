#include <cstdio>
#include <cstring>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "cnfctx.h"
#include "luactx.h"
#include "filereader.h"
#include "fileoff.h"

const size_t FileOff::MAX_FILENAME_LENGTH = 256;

FileOff::FileOff()
{
  cnf_  = 0;
  addr_ = MAP_FAILED;
}

FileOff::~FileOff()
{
  if (addr_ != MAP_FAILED) munmap(addr_, length_);
}

bool FileOff::loadFromFile(char *errbuf)
{
  FILE *fp = fopen(file_.c_str(), "r");
  if (!fp) {
    if (errno != ENOENT) {
      snprintf(errbuf, MAX_ERR_LEN, "FileOff load error %s", strerror(errno));
      return false;
    } else {
      return true;
    }
  }
  FileOffRecord record;
  while (fread(&record, sizeof(record), 1, fp) == 1) {
    if (record.inode == 0 && record.off == 0) continue;
    map_.insert(std::make_pair(record.inode, record.off));
  }

  fclose(fp);
  return true;
}

bool FileOff::init(CnfCtx *cnf, char *errbuf)
{
  cnf_  = cnf;
  addr_ = MAP_FAILED;
  file_ = cnf->libdir() + "/fileoff";

  if (!loadFromFile(errbuf)) return false;
  return true;
}

bool FileOff::reinit()
{
  length_ = sizeof(FileOffRecord) * cnf_->getLuaCtxs().size();

  if (addr_ != MAP_FAILED) {
    munmap(addr_, length_);
    addr_ = MAP_FAILED;
  }

  int fd = open(file_.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
  if (fd == -1) {
    snprintf(cnf_->errbuf(), MAX_ERR_LEN, "open %s error %s", file_.c_str(), strerror(errno));
    return false;
  }

  if (ftruncate(fd, length_) == -1) {
    snprintf(cnf_->errbuf(), MAX_ERR_LEN, "ftruncate %s error %s", file_.c_str(), strerror(errno));
    close(fd);
    return false;
  }

  addr_ = mmap(NULL, length_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr_ == MAP_FAILED) {
    snprintf(cnf_->errbuf(), MAX_ERR_LEN, "mmap %s error %s", file_.c_str(), strerror(errno));
    close(fd);
    return false;
  }

  char *ptr = (char *) addr_;
  for (std::vector<LuaCtx *>::iterator ite = cnf_->getLuaCtxs().begin(); ite != cnf_->getLuaCtxs().end(); ++ite) {
    (*ite)->getFileReader()->initFileOffRecord((FileOffRecord *) ptr);
    ptr += sizeof(FileOffRecord);
  }
  memset(ptr, 0x00, (length_ - (ptr - (char *) addr_)));
  return true;
}

off_t FileOff::getOff(ino_t inode) const
{
  std::map<ino_t, off_t>::const_iterator pos = map_.find(inode);
  if (pos == map_.end()) return -1;
  else return pos->second;
  return (off_t) -1;
}

bool FileOff::setOff(ino_t inode, off_t off)
{
  std::map<ino_t, off_t>::iterator pos = map_.find(inode);
  if (pos == map_.end()) return false;

  pos->second = off;
  return true;
}
