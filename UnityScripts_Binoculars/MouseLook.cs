using UnityEngine;
using System.Collections;
using System.Runtime.InteropServices;
/// MouseLook rotates the transform based on the mouse delta.
/// Minimum and Maximum values can be used to constrain the possible rotation

/// To make an FPS style character:
/// - Create a capsule.
/// - Add the MouseLook script to the capsule.
///   -> Set the mouse look to use LookX. (You want to only turn character but not tilt it)
/// - Add FPSInputController script to the capsule
///   -> A CharacterMotor and a CharacterController component will be automatically added.

/// - Create a camera. Make the camera a child of the capsule. Reset it's transform.
/// - Add a MouseLook script to the camera.
///   -> Set the mouse look to use LookY. (You want the camera to tilt up and down like a head. The character already turns.)
[AddComponentMenu("Camera-Control/Mouse Look")]
public class MouseLook : MonoBehaviour {
	
	public GameObject sys;
	private MainScript mainScript;
	
	public enum RotationAxes {MouseXAndY = 0, MouseX = 1, MouseY = 2, MouseZ = 3 }
	public RotationAxes axes = RotationAxes.MouseXAndY;
	public float sensitivityX = 7F;
	public float sensitivityY = 7F;

	public float minimumX = -360F;
	public float maximumX = 360F;

	public float minimumY = -90F;
	public float maximumY = 90F;

	float rotationY = 0F;
	float rotationX = 0F;
	float rotationZ = 0F;
	
	public float calibX = 0.0F;
	public float calibY = 0.0F;
	public float calibZ = 0.0F;
	
	public float offsetX = 0.0F;
	public float offsetY = 335.0F;
	public float offsetZ = 0.0F;

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getRoll ();

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getPitch ();

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getYaw ();

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getDragX ();

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getDragY ();

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getPSQuatW ();

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getPSQuatX ();

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getPSQuatY();

    [DllImport("RenderingPlugin_Binoculars")]
	private static extern float getPSQuatZ ();
	
	Quaternion mobileDeviceRotation;
	Quaternion phaseSpaceRotation;
	void Awake()
	{
		sys = GameObject.Find("System");
		mainScript = sys.GetComponent<MainScript>();
		mobileDeviceRotation = Quaternion.identity;
		phaseSpaceRotation = Quaternion.identity;
	}
	
	bool isFirstTouch = true;
	float firstTouchX = 0.0f;
	float firstTouchY = 0.0f;
	
	float oldDragX = 0.0f;
	float oldDragY = 0.0f;

	
	void Update ()
	{
		

		if(mainScript.getExperimentInterface() != (int)MainScript.interfaces.WIM)
		{
			float roll = getRoll ();
			float pitch = getPitch ();
			//float yaw = -getYaw (); //minus to turn phone 180 degrees on other landscape
			float yaw = getYaw ();
//			float phaseSpaceX = getPSQuatX();
//			float phaseSpaceY = getPSQuatY();
//			float phaseSpaceZ = getPSQuatZ();
//			float phaseSpaceW = getPSQuatW();
			
			mobileDeviceRotation = Quaternion.Euler (pitch /*- calibY*/, roll /*- calibX + 335f*/, yaw); // 335 is the rotation halfway between the two scene cameras (used for calibration)
//			phaseSpaceRotation = new Quaternion(phaseSpaceX, phaseSpaceY, phaseSpaceZ, phaseSpaceW);
			
			Vector3 mobileDeviceRotEuler = mobileDeviceRotation.eulerAngles;
//			Vector3 PSRotEuler = phaseSpaceRotation.eulerAngles;
			
			mobileDeviceRotEuler.x = mobileDeviceRotEuler.x - calibX + offsetX;
			mobileDeviceRotEuler.y = mobileDeviceRotEuler.y - calibY + offsetY;
			mobileDeviceRotEuler.z = mobileDeviceRotEuler.z - calibZ + offsetZ;

//			PSRotEuler.x = PSRotEuler.x - calibX + offsetX;
//			PSRotEuler.y = PSRotEuler.y - calibY + offsetY;
//			PSRotEuler.z = PSRotEuler.z - calibZ + offsetZ;
//			
			mobileDeviceRotation = Quaternion.Euler (mobileDeviceRotEuler);
//			phaseSpaceRotation = Quaternion.Euler (PSRotEuler);
			
			transform.rotation = mobileDeviceRotation;
//			transform.rotation = phaseSpaceRotation;

				
			//transform.localRotation = Quaternion.Euler (30f,30f,0f);
			//transform.localEulerAngles = new Vector3(30f,30f,0f);
//			if (axes == RotationAxes.MouseX)
//			{
//	
//				
//				//rotationX += Input.GetAxis("Mouse X") * sensitivityX;
//				rotationX = roll - calibX + 330.0f;
//				rotationX = Mathf.Clamp (rotationX, minimumX, maximumX);
//				transform.localEulerAngles = new Vector3(0, rotationX, 0);
//				//Debug.Log ("X: " + Input.GetAxis("Mouse X"));
//			}
//			else if (axes == RotationAxes.MouseY)
//			{
//				//rotationY += -1*Input.GetAxis("Mouse Y") * sensitivityY;
//				rotationY = pitch - calibY;
//				rotationY = Mathf.Clamp (rotationY, minimumY, maximumY);
//				transform.localEulerAngles = new Vector3(rotationY, 0, 0);
//				
//			}
//			else
//			{

//				//rotationZ += Input.GetAxis("Mouse Y") * sensitivityY;	
//				rotationZ = yaw - calibZ;
//				transform.localEulerAngles = new Vector3(0, 0, rotationZ);
//			}
//			
			if(Input.GetKeyDown("c"))
			{

				calibY = roll; //- 180;
				calibX = pitch; //- 180;
				calibZ = yaw;

				//calibY = PSRotEuler.y;//roll;
				//calibX = PSRotEuler.x;//pitch;
			}
			
			//Offsets should be continuously updated with PhaseSpace
			//Something like:
//			calibX = PSRotEuler.x - pitch;
//			calibY = PSRotEuler.y - roll;
//			calibZ = PSRotEuler.z - yaw;
			//Debug.Log(roll + "  "+ pitch + " " + yaw);
			
		}
		else
		{
			float dragX = getDragX ();
			float dragY = getDragY ();
			
			if(axes == RotationAxes.MouseZ)
			{
				transform.localEulerAngles = new Vector3(0, 0, 0);	
			}
			
			if(dragX != 0 && dragY != 0)
			{
				if(isFirstTouch)
				{
					oldDragX = dragX;
					oldDragY = dragY;
					isFirstTouch = false;
				}
				else if (axes == RotationAxes.MouseX)
				{

					rotationX += dragX - oldDragX;
					oldDragX = dragX;
					//rotationX = Mathf.Clamp (rotationX, minimumX, maximumX);
					transform.localEulerAngles = new Vector3(0, -rotationX/sensitivityX, 0);
					
					//transform.Rotate(Vector3.right, rotationX);
					
				}
				else if (axes == RotationAxes.MouseY)
				{
					rotationY += dragY - oldDragY;
					oldDragY = dragY;
					rotationY = Mathf.Clamp (rotationY, minimumY, maximumY);
					transform.localEulerAngles = new Vector3(rotationY/sensitivityY, 0, 0);
					//transform.Rotate(Vector3.up, rotationY);
				}

			}
			else
			{
				isFirstTouch = true;
			}
		}
		 
		
		//transform.localEulerAngles = new Vector3(rotationY, rotationX, 0);
		//transform.Rotate(new Vector3(roll, pitch, yaw));
	}

	void Start ()
	{
		// Make the rigid body not change rotation
		if (GetComponent<Rigidbody>())
			GetComponent<Rigidbody>().freezeRotation = true;
			
	}
}