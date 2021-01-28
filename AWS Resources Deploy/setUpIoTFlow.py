###################################################
#This Script does the following in sequence:
# 1. Creates IoT Thing with keys and certs and downlod them in main/certs folder
# 2. Create IoT Analytics Resources 
# 3. Create Role to be assumed by IoT Core for Rule
# 4. Create IoT Analytics Rule

#Before running this script make sure:
#1. You have AWS CLI V2 Installed and configured using 'aws configure'
#2. User has Administrator policy
###################################################
import boto3
import botocore.exceptions as exceptions
import logging
import os
from pathlib import Path

#Users account and region
account = boto3.client('sts').get_caller_identity().get('Account')
region =  boto3.session.Session().region_name

#Amazon Root CA Certificate
rootCACert = """-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----"""

#SQL for IoT rule
ruleSQL = """SELECT *, parse_time("DD/MM/YYYY HH:mm:ss", timestamp(), "Europe/Berlin" )
              as my_timestamp, timestamp() as unixtime FROM 'esp32/traffic/data'"""

#SQL for IoT Analytics Data Set
dataSetSQL = """SELECT __dt, my_timestamp, flowSegmentData.freeFlowSpeed, 
                flowSegmentData.currentSpeed from esp32_traffic_data_datastore order 
                by my_timestamp desc"""

#Directory to save Device certs and keys
newDir = Path(__file__).parent / "../main/certs"

#Thing Parameters
thingName = 'ESP32DevKit-C'
policyName='ESP32Policy'
thingPolicy = """{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "iot:Connect",
        "iot:Publish",
        "iot:Subscribe",
        "iot:Receive"
      ],
      "Resource": [
        "*"
      ]
    }
  ]
}"""

#IoT Analytics Parameters
ioTAnalyticschannelName = 'ESP32TrafficDataChannel'
ioTAnalyticsDataStoreName = 'ESP32TrafficDataStore'
ioTAnalyticsRulePolicy = """{
                            "Version": "2012-10-17",
                            "Statement": [
                                {
                                    "Effect": "Allow",
                                    "Action": "iotanalytics:BatchPutMessage",
                                    "Resource": [
                                        "arn:aws:iotanalytics:{region}:{account}:channel/{ioTAnalyticschannelName}" 
                                    ]
                                }
                            ]
                          } """.format(region = region, account = account, ioTAnalyticschannelName = ioTAnalyticschannelName)

# Set up our logger
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger()

def savecredentialsData(credentialsData):
  if not os.path.exists(newDir):
      os.makedirs(newDir)
  else:      
      certPath = os.path.join(newDir, "certificate.pem.crt") 
      file = open(certPath, "w")
      file.write("%s\n" %(credentialsData["certificatePem"]))
      file.close()
      
      rootCAPath = os.path.join(newDir, "aws-root-ca.pem") 
      file = open(rootCAPath, "w")
      file.write("%s\n" %(rootCACert))
      file.close()
      
      keyPath = os.path.join(newDir, "private.pem.key") 
      file = open(keyPath, "w")
      file.write("%s\n" %(credentialsData["keyPair"]["PrivateKey"]))
      file.close()

def createThing(iot):
  # Let's use Amazon S3
  
  iot = boto3.client('iot')
  try:
      thingData = iot.create_thing(thingName=thingName)
      credentialsData = iot.create_keys_and_certificate(setAsActive=True)
      savecredentialsData(credentialsData)
      #iot.create_policy(policyName=policyName, policyDocument=ThingPolicy)

      iot.attach_thing_principal(thingName=thingName, principal=credentialsData["certificateArn"])
      iot.attach_principal_policy(policyName=policyName, principal=credentialsData["certificateArn"])
      return iot
  except exceptions.ClientError as error:
      logger.error(error)
      raise(error)

def createRoleForIoTAnalyticsRule():
    
  iam = boto3.client('iam')
  try:  
    role = iam.create_role(Path='service-role', RoleName='IoTAnalyticsRuleRole', 
                                   AssumeRolePolicyDocument=' iot.amazonaws.com')
    policy = iam.create_policy(
                                PolicyName='IoTAnalyticsRulePolicy',
                                Path='service-role',
                                PolicyDocument=ioTAnalyticsRulePolicy
                              )                               
    iam.attach_role_policy(
                            RoleName=role['Role']['RoleName'],
                            PolicyArn=policy['Policy']['Arn']
                          )

    return role 
  except exceptions.ClientError as error:
      logger.error(error)
      raise(error)                       

def createIoTAnalyticsResources():
  iotanalytics = boto3.client('iotanalytics')
  try:
    iotanalytics.create_channel(channelName=ioTAnalyticschannelName,
                                  channelStorage=
                                  {
                                  'serviceManagedS3': {}
                                  },
                                  retentionPeriod=
                                  {
                                    'unlimited': False,
                                    'numberOfDays': 365
                                  }
                                )
    
    iotanalytics.create_datastore(datastoreName=ioTAnalyticsDataStoreName,
                                    datastoreStorage=
                                    {
                                      'serviceManagedS3': {}
                                    },
                                    retentionPeriod=
                                    {
                                      'unlimited': False,
                                      'numberOfDays': 365
                                    },
                                    fileFormatConfiguration=
                                    {
                                      'jsonConfiguration': {}
                                    }
                                  )

    iotanalytics.create_pipeline(pipelineName='ESP32TrafficDataPipeline',
                                 pipelineActivities=[
                                                      {
                                                          'channel': 
                                                          {
                                                              'name': 'DataSource',
                                                              'channelName': ioTAnalyticschannelName,
                                                              'next': 'RemoveAttributes'
                                                          },
                                                          'removeAttributes': 
                                                          {
                                                              'name': 'RemoveAttributes',
                                                              'attributes': ['@version','coordinates'],
                                                              'next': 'DataStore'
                                                          },
                                                          'datastore': 
                                                          {
                                                              'name': 'DataStore',
                                                              'datastoreName': ioTAnalyticsDataStoreName
                                                          }
                                                      },
                                                    ]
                                )

    iotanalytics.create_dataset(
                                datasetName='ESP32TrafficDataSet',
                                actions=[
                                          {
                                              'actionName': 'DataSetAction',
                                              'queryAction': {
                                                              'sqlQuery': dataSetSQL,
                                                            },
                                          },
                                        ],
                                        triggers=[
                                            {
                                                'schedule': {
                                                    'expression': 'cron(0 23 * * ? *)'
                                                },
                                            },
                                        ],
                                 )
  except exceptions.ClientError as error:
      logger.error(error)
      raise(error)                                                                                     


def createIoTRule(iot, role):
  try:      
    iot.create_topic_rule(ruleName='ESP32IoTAnalyticsRule',
                          topicRulePayload=
                            {
                              'sql': ruleSQL,
                              'actions': 
                                [
                                  {
                                   'iotAnalytics': 
                                    {
                                      'channelName': ioTAnalyticschannelName,
                                      'batchMode': False,
                                      'roleArn': role['Role']['Arn']
                                    } 
                                  } 
                                ],
                              'ruleDisabled': False,
                              'awsIotSqlVersion': 'string', 
                            }
                          )
  except exceptions.ClientError as error:
      logger.error(error)
      raise(error)                        

if __name__ == "__main__":
    
    logger.info("Creating Thing...")
    iot = createThing()

    logger.info("Creating IoT Analytics Resources...")
    createIoTAnalyticsResources()

    logger.info("Creating Role IoT Analytics Rule...")
    role = createRoleForIoTAnalyticsRule()

    logger.info("Creating IoT Analytics Rule...")
    createIoTRule(iot, role)
    
