#include "header.h"
#include <stdio.h>
#include <stdlib.h>





#define  size  5

int items[size];
int front = -1;
int rear = -1;

int isempty()
  {

  if(rear == -1)
    return 1;
  else
    return 0;
  }

int isfull()
  {
      if(front==0 && rear == size-1 | front==rear +1)
        return 1;
      else 
        return 0;
      
      
  }

  int enqueue(int element)
  {
    if(isfull){
      return 1;
    } 
    else{
      if(front=-1){
        front=0;
      }
      rear = rear+1 % size;
      items[rear]= element;
      }
  }

    int dequeue()
    {
      int element;
      if(isempty()){
        return 1;
      }
      else{
        element = items[front];
        if(front==rear)
        {
        front= -1;
        rear = -1;
         }
        else
        {
          front = front +1 % size;
        }
        return element;
         }
      }




void printsize(){
  int i ;
  if(isempty())
  {
    printf("queue is empty");

  }
  else
  {
    printf("first element is %d",front);
    for(i=front; i!= rear; i=(i+1)%size){
      printf("elements are %d",items[i]);

    }
    printf("last item %d",items[i]);

  }


}








