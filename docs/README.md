# Mini-UnionFS

A simplified Union File System in userspace using FUSE.

## Build
```bash
sudo apt install libfuse-dev fuse build-essential
make
```

## Run Demo
```bash
make mount
# or manually:
mkdir -p lower upper mnt
echo "base content" > lower/base.txt
./mini_unionfs lower upper mnt

# In another terminal:
ls mnt/           # merged view
echo "edit" >> mnt/base.txt   # triggers CoW
rm mnt/base.txt               # creates upper/.wh.base.txt
```

## Run Tests
```bash
bash tests/test_unionfs.sh
```

## Unmount
```bash
make unmount
# or: fusermount -u mnt
```

## Team
- Member 1: Infrastructure, FUSE boilerplate, resolve_path()
- Member 2: Read operations (getattr, readdir, read)
- Member 3: Write operations + Copy-on-Write engine
- Member 4: Deletion + Whiteouts + Tests + Design Doc
