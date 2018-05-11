# libmad_port
 [libmad mp3 decoder](https://sourceforge.net/projects/mad/files/libmad/)

 本仓库是使用 libmad 进行解码 mp3 文件的例子。

 - main-v1.c 一次读取整首歌曲到内存
 - main-v2.c 一次仅读取4K，解完之后再读4K，减少内存消耗
