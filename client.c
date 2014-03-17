
/*
client:  # client host:port/filename
    open file
    connect to host
    send file request
    get status and filesize (timeout 15sec)
    if status != OK
        quit(status)
    in loop:
        get data (timeout 15sec)
        save them to file
    close connection and file
    quit

protocol (text,TCP):
    c->s:
        FILENAME: filename.txt
    s->c:
        STATUS: OK, NOT_FOUND, ERROR
        FILESIZE: file size [bytes])
        CONTENT:
        data in binary
*/
