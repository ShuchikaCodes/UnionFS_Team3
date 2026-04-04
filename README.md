#  Mini-UnionFS 
The objective of this project is to implement Mini-UnionFS, a simplified 
version of a Union File System, using FUSE (Filesystem in Userspace). The 
goal is to simulate the layered filesystem approach used in container 
technologies such as Docker. 
In container systems, multiple filesystem layers are stacked together to 
create a unified view without copying entire file systems. Instead of 
duplicating files, containers maintain a read-only base image and add a 
read-write layer on top. This improves efficiency, reduces storage usage, 
and enables isolation between containers.  
In this project, we will recreate this concept by merging multiple directories 
into a single mounted virtual filesystem.
