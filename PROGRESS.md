### before 4th October 2026
- spent the past few days researching how debuggers work, including call stacks, program states, and how execution history can be recorded and replayed.
- studied the project specififcations and wokred on building foundational logic for the code 
- worked on figuring out which data structures to use for each stage

### 4 October 2026
- Set up VS Code for C++ development on Windows using the g++ compiler with debugging working through gdb
-  Set up Linux through WSL (Ubuntu) with g++, gdb, make and git, since the project must also run on Linux.

### 5 October 2026
- implementing the Stack class but instead of nodes i am using vectors instead since i have a better understanding of working with them in stacks 
- added a test file where i can just test my code blocks as i write them so debugging is easier (this file can be ignored since this is jsut to test my code nothing else and ill be deleting code that i have tested)
- implemented the TimeLine class
- implemented readline, first and second word

### 7 October 2026
- validating the program using stacks (similar to the bracket validation problem)
- wrote the write to resovlve function but this took me longer since i had to figure out of the fwrite() function worked since i have never worked it with it before and i wanted to use ofstream but stuck with FILE* since that is what the function already used (still a little confused about the FILE*/fstream situation)
-read resolve implementation 
- done with resolveProgram (took me too long to understand this and figure out whats happening but i think i am  kind of enjoying this project now which is the biggest progress made so far in my opinion........)

### 10 October 2026
- initially when writing the resolve i had just stored 0 as the offset non call lines and the call lines offset was the postion of the fucntion it was calling but now i need to figure out how to do this exactly how the PDF demands. 
- how i am changing it now is that instead of writing 0 each line will gets its own offset positoon first and then when the patching the call lines offset will get replaced by the offset of the function it is calling this way we wont have to mess with the text cause that will then change the length of the line and i have tired but i cant figure out how that would work so this solution seems to work and full fill the requirement of the PDF asw. (please let me do this like this thanks ur amazing)
- implemented tonkenize line
- added some helper fucntions for variable handling 

### 11 October 2026
- I have added a struct to keep track of the call line variable. Baically the variable whos values changes afternthe fucntion is called. So the var we send into the fucntion from the main we need to reflect the changes in main asw so this and with another stack thats keeping track of these variables i will be able to do that. 
- changed the return typr of execute program from void to bool because if the the argument count doesnt match for the call line and the fucntion header i want it to return false

### 12 October 2026
- added a helping fucntion for wrtieTdbg so we can write strings. a string cant be saved to the file directly because the computer will only keep a note pointing to the letters which will be useless late. The helper fucntion will save the real letters instead with the length first so when we are reading it later we will know how mnay letters to read back. 
-writeTdbg implementation