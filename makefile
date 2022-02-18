#Executable name
TARGET=NewTsim
#Compiler binary
CC=gcc
#Compiler Flags									
CFLAGS=-std=c89 -pedantic -D_GNU_SOURCE -Werror -lrt			
OBJS = master.o node.o user.o shared/so_conf.o shared/so_ipc.o shared/so_random.o

#Compiles outdated modules and links them to deploy the target
#If there are some missing .o files, make compiles their .c and later on
#it runs the linking between object files
$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(CFLAGS) -o $(TARGET)

#Compila agevolmente con i valori a compile time di SO_BLOCK_SIZE e SO_REGISTRY_SIZE relativi
#N.B. Per eseguire totalmente con i valori della configurazione, eseguire con -NewTsim -c <num_conf>
conf1: CFLAGS += -DCONF1
conf2: CFLAGS += -DCONF2
conf3: CFLAGS += -DCONF3
conf1 conf2 conf3: clean $(TARGET)
#Deletes both target executable and target conf.json file
#Clears the project directory from object files
clean:
	find -type f -name "*.o" -delete
	find -type f -name "$(TARGET)" -delete

