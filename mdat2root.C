// Convert qmesydaq mdat files to ROOT format
// For use with the new TofTof readout
// This is a modified version of the MTSD format - some unused bits are repurposed
// Since we have only digital (TTL) input the amplitude is ignored, for example

// -------------------------------------------------------------------//
// ------------------------- Global variables ------------------------//
// -------------------------------------------------------------------//

// Masks for extracting parameters from 48-bit event block
// eventID: 0 for 'neutron' events (tube signals), 1 for trigger (e.g. monitor, chopper) events
// modID: MTSD module ID inside a single MCPD module, values from 0-7
// slotID: Channel ID inside a single MPSD module, values from 0-15
// amp: amplitude, calculated internally in the MTSD from the signal amplitude
// xpos: not used in the MTSD format
// time: 19 bit fine timestamp (counts up to 52.4 ms with 100-ns time bins). Combine with the header timestamp to get the absolute event time.
ULong64_t mask_eventID =    0b100000000000000000000000000000000000000000000000;
ULong64_t mask_modID =      0b011100000000000000000000000000000000000000000000;
ULong64_t mask_slotID =     0b000001111000000000000000000000000000000000000000;
ULong64_t mask_amp =        0b000000000001111111100000000000000000000000000000;
ULong64_t mask_xpos =       0b000000000000000000011111111110000000000000000000;
ULong64_t mask_time =       0b000000000000000000000000000001111111111111111111;

// Note that the amp mask should have two extra bits when not modified
// ULong64_t mask_amp =        0b000000000111111111100000000000000000000000000000;

// Masks for extracting data from trigger event blocks (eventID==1)
// trigID: event trigger source, timer 1-4 get ID 1-4, rear panel TTL inputs get ID 5-6, ID 7 is for compare register (?)
// dataID: data source, front panel inputs 0-3 get ID 0-3, rear inputs get ID 4-5, ADC1,2 get ID 6,7
// tData: Counter, timer or ADC value of source. Dpeending on source, not all bits may be valid
ULong64_t mask_trigID =  0b011100000000000000000000000000000000000000000000;
ULong64_t mask_dataID =  0b000011110000000000000000000000000000000000000000;
ULong64_t mask_tData =   0b000000001111111111111111111110000000000000000000;
// Time mask is same as for event data


// Header parameters
struct Header{
  uint16_t bufferlength;
  uint16_t buffertype;
  uint16_t headerlength;
  uint16_t buffernumber;
  uint16_t runID;
  uint8_t  mcpdID;          // Starting from 0
  uint8_t  status;          //
  uint64_t headerTS;        // 48 bit timestamp
  uint64_t param0;          // 48 bit parameter - unused
  uint64_t param1;          // 48 bit parameter - unused
  uint64_t param2;          // 48 bit parameter - unused
  uint64_t param3;          // 48 bit parameter - unused
} header;

// Individual event parameters
struct Event{
  uint16_t xpos;           // Position along tube
  uint16_t tubeID;           // Tube location
  uint16_t modID;          // ID of the MPSD
  uint16_t slotID;         // ID of the tube
  uint16_t amp;            // Combined signal amplitude (both tube signals summed)
  uint64_t time;           // Time stamp, units of 100 ns 
  uint8_t eventID;         // 0 for real events, 1 for self triggers
  uint32_t eventTS;        // The 19 bit time stamp within the buffer
  uint16_t trigID;	   // For trigger events (eventID==1) the trigger source
  uint16_t dataID;	   // For trigger events (eventID==1) the data source
  uint32_t tData;  	   // For trigger events (eventID==1) the data package - some data sources may not use all 21 bits available
};  //event, event0;        // use event to write current value and event0 as a template to reset the struct after writing to file

Event event;
Event event0 = {};

// -------------------------------------------------------------------//
// --------------------- File reading functions ----------------------//
// -------------------------------------------------------------------//

// Byteswap a two-byte word
void ByteSwap16(uint16_t &word){
  word = (word)<<8 | (word)>>8;
}

// Read in a single two-byte word and byteswap (calling byteswap function)
void ReadWord(ifstream &infile, uint16_t &word){
  infile.read((char*) &word, 2);
  ByteSwap16(word);
}

// Read in a single byte
void ReadByte(ifstream &infile, uint8_t &byte){
  infile.read((char*) &byte, 1);
}

// Read a six-byte event, consisting of 3 words
// Used for event data and some header parameters
void ReadEntry(ifstream &infile, uint64_t &entry){
  uint16_t low, mid, high;
  ReadWord(infile, low);
  ReadWord(infile, mid);
  ReadWord(infile, high);
  entry = (uint64_t)low | (uint64_t)mid<<16 | (uint64_t)high<<32;
} 

// Read (and dispose of) 58 bytes of file header
void ReadHeader(ifstream &infile){ 
  char buffer[58];
  infile.read(buffer, 58);
}

// Read in an event buffer
int ReadBuffer(ifstream &infile){

  ReadWord(infile, header.bufferlength);  // Length of the buffer
  ReadWord(infile, header.buffertype);    // Should be 0x0001
  if(header.buffertype != 0x0001){
    return 1;
  }
  ReadWord(infile, header.headerlength);  // Should be constant
  ReadWord(infile, header.buffernumber);
  ReadWord(infile, header.runID); 
  ReadByte(infile, header.mcpdID);
  ReadByte(infile, header.status);
  ReadEntry(infile, header.headerTS);
  ReadEntry(infile, header.param0);
  ReadEntry(infile, header.param1);
  ReadEntry(infile, header.param2);
  ReadEntry(infile, header.param3);

  return 0;

}

// Read a single 48 bit event and split into the component parts
void ReadEvent(ifstream &infile){
  uint64_t rawevent;
  ReadEntry(infile, rawevent);
  event.eventID = (rawevent & mask_eventID) >> 47;
  // Parameters to be read depend on the event type
  if (event.eventID==0){
    event.amp = (rawevent & mask_amp) >> 29;
    event.tubeID = ((rawevent & mask_slotID) >> 39) | ((rawevent & mask_modID) >> 41);
    event.xpos = (rawevent & mask_xpos) >> 19;
    event.modID = (rawevent & mask_modID) >> 44;
    event.slotID = (rawevent & mask_slotID) >> 39;
    // tubeID = (mcpdID-1)*64 + modID*16 + slotID
    event.tubeID= ((header.mcpdID - 1) << 6 | event.modID << 4 | event.slotID);
  }
  else if (event.eventID==1){
    event.trigID = (rawevent & mask_trigID) >> 44;
    event.dataID = (rawevent & mask_dataID) >> 40;
    event.tData = (rawevent & mask_tData) >> 19;
  } 
  event.eventTS = (rawevent & mask_time);
  event.time = event.eventTS + header.headerTS;
}

void ReadBufferEnd(ifstream &infile, int debug){
  uint16_t word;
  if ((debug & 4) > 0) cout << "--- Buffer padding ---" << endl;
  for (int i = 0; i < 4; i++){
    ReadWord(infile, word);
    if ((debug & 4) > 0) cout << hex << word << endl;
  }
  
}

// Print the current event buffer
void PrintBuffer(){
  cout << "----------------------------------------------------" << endl;
  cout << "Buffer number: " << header.buffernumber << endl;
  cout << "Buffer length: " << header.bufferlength << endl;
  cout << "Expected number of entries: " << (header.bufferlength -21)/3 << endl;
  cout << "Header length: " << header.headerlength << endl;
  cout << "Run ID: " << header.runID << endl;
  cout << "MCPD ID: " << int(header.mcpdID) << endl;
  cout << "Status: " << int(header.status) << endl;
  cout << "Header timestamp: " << header.headerTS << endl;
  cout << "Parameter 0: " << header.param0 << endl;
  cout << "Parameter 1: " << header.param1 << endl;
  cout << "Parameter 2: " << header.param2 << endl;
  cout << "Parameter 3: " << header.param3 << endl;
  cout << "----------------------------------------------------" << endl;
}

// Print the current event information
void PrintEvent(){
  cout << "----------------------------------------------------" << endl;
  cout << "EventID: " << int(event.eventID) << endl;
  cout << "xpos: " << event.xpos << endl;
  cout << "tubeID: " << event.tubeID << endl;
  cout << "modID: " << event.modID << endl;
  cout << "slotID: " << event.slotID << endl;
  cout << "amp: " << event.amp << endl;
  cout << "trigID: " << event.trigID << endl;
  cout << "dataID: " << event.dataID << endl;
  cout << "tData: " << event.tData << endl;
  cout << "time stamp: " << event.eventTS << endl;
  cout << "absolute time: " << event.time << endl;
  cout << "----------------------------------------------------" << endl;
}

// -------------------------------------------------------------------//
// ------------------------------ Main -------------------------------//
// -------------------------------------------------------------------//


// debug 0 = off, 1 = buffer, 2 = events, 4 = post-buffer padding, 7 = all
void mdat2root(TString filename, int debug=0){

  uint64_t total_events = 0;  // Total event counter
  uint64_t buffer_num = 0;    // Current buffer number
  uint64_t entry_num = 0;     // Current entry (event) number
  
  //--- Create the output ROOT file ---//

  // Set output filename
  TString outfilename = filename;
  outfilename.ReplaceAll(".mdat",".root");  
  
  TFile *outfile = new TFile(outfilename,"RECREATE");
  TTree *rawdata = new TTree("rawdata","Raw data converted from mdat to ROOT");

  // Set up branches - link to struct parameters
  rawdata->Branch("xpos", &event.xpos, "xpos/s");
  rawdata->Branch("tubeID", &event.tubeID, "tubeID/s");
  rawdata->Branch("modID", &event.modID, "modID/s");
  rawdata->Branch("slotID", &event.slotID, "slotID/s");
  rawdata->Branch("amp", &event.amp, "amp/s");
  rawdata->Branch("time", &event.time, "time/l");
  rawdata->Branch("eventID", &event.eventID, "eventID/b");
  rawdata->Branch("trigID", &event.trigID, "trigID/s");
  rawdata->Branch("dataID", &event.dataID, "dataID/s");
  rawdata->Branch("tData", &event.tData, "tData/i");
  rawdata->Branch("eventTS", &event.eventTS, "eventTS/i");
  rawdata->Branch("mcpdID", &header.mcpdID, "mcpdID/b");
  rawdata->Branch("status", &header.status, "status/b");
  rawdata->Branch("param0", &header.param0, "param0/l");
  rawdata->Branch("param1", &header.param1, "param1/l");
  rawdata->Branch("param2", &header.param2, "param2/l");
  rawdata->Branch("param3", &header.param3, "param3/l");  
  rawdata->Branch("headerTS", &header.headerTS, "headerTS/l");
  rawdata->Branch("buffernumber", &header.buffernumber, "buffernumber/s"); 


  //--- Open the input mdat file ---//
  
  ifstream infile(filename, ios::in | ios::binary); 
  
  // Read past the file header, 58 bytes
  ReadHeader(infile);
  
  // Loop over all buffers - break loop when the wrong buffer header type is found
  
  while(true){
  
    if (ReadBuffer(infile) == 1) break;
    else buffer_num++;
    
    if ((debug & 1) > 0) PrintBuffer();
    
    int buffer_entries = (header.bufferlength - 21) / 3; // Expected number of entries in the current buffer
    
    for (int bentry = 0; bentry < buffer_entries; bentry++){
      
      ReadEvent(infile);
      if ((debug & 2) > 0) PrintEvent();
      entry_num++;
      rawdata->Fill();
      
      // Set all event parameters back to zero
      event = event0;
      
      // Print info on status
      if (entry_num % 10000 == 0){
        cout << "Processing entry number: " << entry_num << "\r" << flush;
      }
    }
    
    // Read past the end-of-buffer words
    ReadBufferEnd(infile, debug);
  
  }
  
  cout << "---------------------------------------------------------" << endl;
  cout << "A total of " << entry_num << " events were read from " << buffer_num << " buffers" << endl; 
  cout << "---------------------------------------------------------" << endl;

  outfile->Write();

  // Close files
  infile.close();
  outfile->Close();
}
