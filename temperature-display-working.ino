#include <DHT.h>
#include <DHT_U.h>
#include "freertos/freertos.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <LiquidCrystal.h>

#define RING_BUFFER_SIZE 15

LiquidCrystal lcd(19, 23, 18, 17, 16, 15);
DHT dht(26, DHT11);

//Globals
QueueHandle_t queue;
SemaphoreHandle_t mutexT1T2;
SemaphoreHandle_t mutexT2T3;
UBaseType_t maxCount;


// [][][][][][][][][][][][] Ring Buffer Stuff [][][][][][][][][][][][][][][]

//define the struct for the ring buffer ----
typedef struct {
  float ringbuffer[RING_BUFFER_SIZE];
  int head;
  int tail;
  SemaphoreHandle_t semaCount;
} RingBuffer;

//actually instantiate the ring buffer struct
RingBuffer temperBuffer;

//ring buffer initializer function -------------
void RingBuffer_Init(RingBuffer* rb) {
  rb->head = 0;
  rb->tail = 0;
  //create counting semaphore for ring buffer between loadTemp and displayTemp task 
  rb->semaCount = xSemaphoreCreateCounting(RING_BUFFER_SIZE, 0);
  if(rb->semaCount != NULL ) {
      Serial.println("Ring buffer semaphore created.");
  }
  
}

//write to ring buffer function ------
void RingBuffer_Write(RingBuffer* rb, float temperdata) {

  //start overwriting of oldest value, BUT check if mutex is available for the rb
  if (xSemaphoreTake(mutexT2T3, portMAX_DELAY) == pdTRUE) {
    
    int nextHead = (rb->head+1) % RING_BUFFER_SIZE;
    if (nextHead != rb->tail) {
      rb->ringbuffer[rb->head] = temperdata;
      rb->head = nextHead;

      //let task3 know there's readings in the ring buffer
      xSemaphoreGive(rb->semaCount);
    }

    //return ring buffer mutex
    xSemaphoreGive(mutexT2T3);

  }

}

//read from ring buffer function ------
int RingBuffer_Read(RingBuffer* rb, float* temperdata) {
    //wait for temp reading in ring buffer
    if (xSemaphoreTake(rb->semaCount, portMAX_DELAY) == pdTRUE) {

        //BUT check if mutex is available for the rb
        if (xSemaphoreTake(mutexT2T3, portMAX_DELAY) == pdTRUE) { 
          *temperdata = rb->ringbuffer[rb->tail]; // Retrieve the data
          rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE; // Move tail forward

          //return ring buffer mutex
          xSemaphoreGive(mutexT2T3);   

          return 1; // Indicate success
        }
    }

    return 0;
}


//(Task 1) - function to pass data from DHT11 to shared queue
void readTemp(void *parameter) {

  while (true) {
    // if (xSemaphoreTake(mutexT1T2, 1) == pdTRUE) {
      
      //read temp from DHT11
      float temp = dht.readTemperature(true);
      //send temp to queue
      xQueueSendToBack(queue, &temp, 1);
      //return mutex
      // xSemaphoreGive(mutexT1T2);
      }

    vTaskDelay(2000);
  // }
}


//(Task 2) - function to pass data from shared queue to shared ring buffer 
void queuetoRing(void *parameter) {

  while (true) {

    //float to store the temp reading from the queue 
    float queueRec;

    //process for reading from DHT11 and putting temp into queue
    // if (xSemaphoreTake(mutexT1T2, portMAX_DELAY) == pdTRUE) {

      //read temp from queue and put in queueRec
      xQueueReceive(queue, &queueRec, portMAX_DELAY);
        
      //write into ring buffer
      RingBuffer_Write(&temperBuffer, queueRec);

      //return queue mutex
      // xSemaphoreGive(mutexT1T2);

    // }
  }
}

//(Task 3) - function to get data from shared ring buffer and print to Serial 
void ringToSerial(void *parameter) {
  
  while (true) {

    //float to store the temp reading from the queue 
    float ringRec; 

    //write into ring buffer
    RingBuffer_Read(&temperBuffer, &ringRec);
    lcd.print(ringRec);
    // Serial.println(ringRec);
    lcd.setCursor(1,1);
    


  }
}



void setup() {
  
  //delay to ensure everything loads
  delay(2000);
  
  //start DHT11 input
  dht.begin();

  //start LCD1602 input
  lcd.begin(16,2);
  lcd.clear();
  lcd.print("Current Temp:");
  lcd.setCursor(1,1);
  //start the serial monitor
  Serial.begin(9600);

  //create the queue for task1 <> task 2
  queue = xQueueCreate(15, sizeof(float));

  //create the ring buffer object for task2 <> task 3
  RingBuffer_Init(&temperBuffer); 

  //create mutexs for queue and ring buffer between task1 <> task 2 <> task 3
  mutexT1T2 = xSemaphoreCreateMutex();
  if( mutexT1T2 != NULL ) {
      Serial.println("Queue mutex created.");
   }
  mutexT2T3 = xSemaphoreCreateMutex();
  if( mutexT2T3 != NULL ) {
      Serial.println("Ring buffer mutex created.");
   }


  xTaskCreate(readTemp, "DHT2Queue", 2048, NULL, 1, NULL);
  xTaskCreate(queuetoRing, "Queue2Ring", 2048, NULL, 1, NULL);
  xTaskCreate(ringToSerial, "Ring2Serial", 2048, NULL, 1, NULL);

}

void loop() {

}
