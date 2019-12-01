# Fifoproc
Implementation of a FIFO using a /proc entry.

## Resources used by the FIFO
- Linked list (cbuffer.c)
    - FIFO-associated temporary storage.
    - Maximum size: 50 bytes.

![image explaining the circular buffer](https://imgur.com/Ln9xfJl.png)
- Two counters to register the number of processes that have opened the FIFO for reading and writing, respectively.
- A mutex to protect the buffer.
    - Uses a semaphore initialized to 1.
    - Mutex associated to "Condition Variables".
- Two "condition variables".
    - One to block the producer and one to block the consumer.
    - To simulate each variable: 
        - Semaphore initialized to 0 (waiting queue)
        - Counter that will record the number of waiting processes (0 or 1)

## Structure

- `fifoproc.c`:
Main file
- `cbuffer.h`:
Declaration of the `cbuffer_t` and operations on it.
- `cbuffer.c`:
Implementation of the `cbuffer_t` operations.




## Behavior 
- Opening FIFO in read mode (consumer) blocks the process until the producer has opened his writing end.
 - Opening FIFO in write mode (producer) blocks the process until the consumer has opened its read end.
 - The producer is blocked if there is no space in the buffer to insert the number of bytes requested by `write()`
 - The consumer is blocked if the buffer contains fewer bytes than requested by `read()`
- If any process blocked at a semaphore is awakened by the
reception of a signal, the operation in question (`open()`, `read()` or `write()`) will return an error
- When all processes (producers and consumers) end their activity with FIFO, the circular buffer has to be emptied. 
- If an attempt is made to read the FIFO when the circular buffer is empty and there are no producers, the module will return the value 0 (EOF).
- If you try to write to FIFO when there are no consumers (read end closed), the module will return an error.


## Execution example

### Terminal 1

```bash
  kernel@debian:~$ sudo insmod ../ProcFifo/fifomod.ko
  kernel@debian:~$ ./fifotest -f /proc/modfifo -s < test.txt
  kernel@debian:~$ 
```

### Terminal 2
```bash
  kernel@debian:~$ ./fifotest -f /proc/modfifo -r
En un lugar de la Mancha, de cuyo nombre no quiero acordarme, no ha mucho tiempo
que vivía un hidalgo de los de lanza en astillero, adarga antigua, rocín flaco y
galgo corredor. Una olla de algo más vaca que carnero, salpicón las más noches,
duelos y quebrantos los sábados, lentejas los viernes, algún palomino de añadidura
los domingos, consumían las tres partes de su hacienda. El resto della concluían
sayo de velarte, calzas de velludo para las fiestas con sus pantuflos de lo mismo,
los días de entre semana se honraba con su vellori de lo más fino. Tenía en su casa
una ama que pasaba de los cuarenta, y una sobrina que no llegaba a los veinte, y
un mozo de campo y plaza, que así ensillaba el rocín como tomaba la podadera...
  kernel@debian:~$
```
