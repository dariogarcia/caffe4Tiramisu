#include <stdio.h>  // for snprintf
#include <string>
#include <vector>

#include "boost/algorithm/string.hpp"
#include "google/protobuf/text_format.h"

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/net.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/util/db.hpp"
#include "caffe/util/io.hpp"
#include "caffe/vision_layers.hpp"

using caffe::Blob;
using caffe::Caffe;
using caffe::Datum;
using caffe::Net;
using boost::shared_ptr;
using std::string;
namespace db = caffe::db;

template<typename Dtype>
int feature_extraction_pipeline(int argc, char** argv);

int main(int argc, char** argv) {
  return feature_extraction_pipeline<float>(argc, argv);
//  return feature_extraction_pipeline<double>(argc, argv);
}

template<typename Dtype>
int feature_extraction_pipeline(int argc, char** argv) {
  ::google::InitGoogleLogging(argv[0]);
  const int num_required_args = 8;
  if (argc < num_required_args) {
    LOG(ERROR)<<
    "This program takes in a trained network and an input data layer, and then"
    " extract features of the input data produced by the net.\n"
    "Usage: extract_features  pretrained_net_param.caffemodel "
    "  feature_extraction_proto_file.prototxt  extract_feature_blob_name1[,name2,...]"
    "  output_directory output_fileName IVFType(1=Char,2=Binary)\n"
    //"  [CPU/GPU] [DEVICE_ID=0]\n"
    "Note: you can extract multiple features in one pass by specifying"
    " multiple feature blob names and dataset names separated by ','."
    " The names cannot contain white space characters and the number of blobs"
    " and datasets must be equal.";
    return 1;
  }
  int arg_pos = num_required_args;

  arg_pos = num_required_args;
  if (argc > arg_pos && strcmp(argv[arg_pos], "GPU") == 0) {
    LOG(ERROR)<< "Using GPU";
    uint device_id = 0;
    if (argc > arg_pos + 1) {
      device_id = atoi(argv[arg_pos + 1]);
      CHECK_GE(device_id, 0);
    }
    LOG(ERROR) << "Using Device_id=" << device_id;
    Caffe::SetDevice(device_id);
    Caffe::set_mode(Caffe::GPU);
  } else {
    LOG(ERROR) << "Using CPU";
    Caffe::set_mode(Caffe::CPU);
  }
  arg_pos = 0;  // the name of the executable
  std::string pretrained_binary_proto(argv[++arg_pos]);

  // Expected prototxt contains at least one data layer such as
  //  the layer data_layer_name and one feature blob such as the
  //  fc7 top blob to extract features.
  /*
   layers {
     name: "data_layer_name"
     type: DATA
     data_param {
       source: "/path/to/your/images/to/extract/feature/images_leveldb"
       mean_file: "/path/to/your/image_mean.binaryproto"
       batch_size: 128
       crop_size: 227
       mirror: false
     }
     top: "data_blob_name"
     top: "label_blob_name"
   }
   layers {
     name: "drop7"
     type: DROPOUT
     dropout_param {
       dropout_ratio: 0.5
     }
     bottom: "fc7"
     top: "fc7"
   }
   */
  std::string feature_extraction_proto(argv[++arg_pos]);
  shared_ptr<Net<Dtype> > feature_extraction_net(
      new Net<Dtype>(feature_extraction_proto, caffe::TEST));
  feature_extraction_net->CopyTrainedLayersFrom(pretrained_binary_proto);

  std::string extract_feature_blob_names(argv[++arg_pos]);
  std::vector<std::string> blob_names;
  boost::split(blob_names, extract_feature_blob_names, boost::is_any_of(","));
  size_t num_features = blob_names.size();

  std::string save_feature_dataset_names(argv[++arg_pos]);
  std::vector<std::string> dataset_names;
  boost::split(dataset_names, save_feature_dataset_names,
               boost::is_any_of(","));
  //CHECK_EQ(blob_names.size(), dataset_names.size()) <<
  //    " the number of blob names and dataset names must be equal";

  for (size_t i = 0; i < num_features; i++) {
    CHECK(feature_extraction_net->has_blob(blob_names[i]))
        << "Unknown feature blob name " << blob_names[i]
        << " in the network " << feature_extraction_proto;
  }

  const char* fileName = argv[++arg_pos];
  //Type of output file
  int outputType = atoi(argv[++arg_pos]);
  const char* className = argv[++arg_pos];

  //int num_mini_batches = atoi(argv[++arg_pos]);
  //This parameter defines the number of images to be processed in parallel. 
  //Currently we assume we process one image at a time
  Datum datum;
  std::vector<Blob<float>*> input_vec;
  std::ofstream file;
  LOG(ERROR)<< "Writing to file: " << dataset_names[0].c_str() << fileName<<".ivf";
  //CHAR
  if(outputType==1) file.open((dataset_names[0]+fileName+".ivf").c_str(),std::ios::trunc);
  //BINARY
  if(outputType==2) file.open((dataset_names[0]+fileName+".ivf").c_str(),std::ios::binary);
  //Write first part of CFG file
  std::ofstream file_cfg;
  LOG(ERROR)<< "Writing to file: " << dataset_names[0].c_str() << fileName<<".cfg";
  file_cfg.open((dataset_names[0]+fileName+".cfg").c_str(),std::ios::trunc);
  file_cfg<<"TYPE:"<<"imageVector\n";
  file_cfg<<"IMAGENAME:"<<fileName<<"\n";
  file_cfg<<"CLASSNAME:"<<className<<"\n";
  file_cfg<<"CAFFEMODEL:"<<pretrained_binary_proto.substr(pretrained_binary_proto.rfind("/")+1,pretrained_binary_proto.length())<<"\n";
  //Extract layers
  feature_extraction_net->Forward(input_vec);
  int total_features = 0;
  int offset = 0;
  for (int i = 0; i < num_features; ++i) {
    const shared_ptr<Blob<Dtype> > feature_blob = feature_extraction_net
        ->blob_by_name(blob_names[i]);
    int dim_features = feature_blob->count();
    const Dtype* feature_blob_data;
  
    total_features+=dim_features;
    LOG(ERROR)<< "Extracting "<<total_features<<" features from blob "<<blob_names[i];
    int layerCounter = 0;
    feature_blob_data = feature_blob->cpu_data() + feature_blob->offset(0);
    for (int d = 0; d < dim_features; ++d) {
      //CHAR
      if(outputType==1){
        if(feature_blob_data[d] > 0) {
            layerCounter++;
            file<<feature_blob_data[d]<<" "<<d+offset<<"\n";
        }
      }
      //BINARY
      if(outputType==2){
        if(feature_blob_data[d] > 0) {
          layerCounter++;
          int position = d + offset;
          float value = feature_blob_data[d]; 
          file.write(reinterpret_cast<char *>(&position), sizeof(position));
          file.write(reinterpret_cast<char *>(&value), sizeof(value));
        }
      }
    }
    //Store layer info to CFG file
    file_cfg<<"LAYER:"<<blob_names[i]<<":"<<dim_features<<":"<<layerCounter<<"\n";
    offset+=dim_features;   
  }  
  file.close();
  file_cfg.close();
  LOG(ERROR)<< "Successfully extracted all features!";
  return 0;
}

