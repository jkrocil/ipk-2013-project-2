
/*
server_main:  # server -d limit [kB] -p port
    create socket on port [port]
    in loop:
        wait for file request from client
        spawn new process to serve the file (limit, port and filename is on stack)
    + KILL&TERM signal will kill subprocesses

server_subprocess:
    if file doesnt exist, send NOT_FOUND
    if unable to open file, send ERROR
    send OK and filesize
    in loop:
        saved_time = current time
        read data of (up to) set limit
        send data
        if EOF, quit
        while saved_time == current_time
            usleep(2000)
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

