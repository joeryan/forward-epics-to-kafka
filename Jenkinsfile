def project = "forward-epics-to-kafka"
def centos = docker.image('essdmscdm/centos-build-node:0.9.1')

node('docker') {
    cleanWs()

    def custom_sh = "/bin/bash"
    def container_name = "${project}-${env.BRANCH_NAME}-${env.BUILD_NUMBER}"
    def run_args = "\
        --name ${container_name} \
        --tty \
        --env http_proxy=${env.http_proxy} \
        --env https_proxy=${env.https_proxy}"

    try {
        container = centos.run(run_args)

        stage('Checkout') {
            sh """docker exec ${container_name} ${custom_sh} -c \"
                git clone https://github.com/ess-dmsc/${project}.git \
                    --branch ${env.BRANCH_NAME}
                git clone -b master https://github.com/ess-dmsc/streaming-data-types.git
            \""""
        }

        stage('Get Dependencies') {
            def conan_remote = "ess-dmsc-local"
            sh """docker exec ${container_name} ${custom_sh} -c \"
                mkdir build
                cd build
                conan remote add \
                    --insert 0 \
                    ${conan_remote} ${local_conan_server}
                conan install --build=missing \
                    --file=../${project}/conan/conanfile-package.txt
            \""""
        }

        stage('Configure') {
            sh """docker exec ${container_name} ${custom_sh} -c \"
                cd build
                cmake3 ../${project} \
                    -DREQUIRE_GTEST=ON \
                    -DCMAKE_BUILD_TYPE=Release \
                    -DCMAKE_SKIP_RPATH=FALSE \
                    -DCMAKE_INSTALL_RPATH='\\\$ORIGIN/../lib' \
                    -DCMAKE_BUILD_WITH_INSTALL_RPATH=TRUE \
                    -DCONAN_SET_OUTPUT_DIRS=ON
            \""""
        }

        stage('Build') {
            sh """docker exec ${container_name} ${custom_sh} -c \"
                cd build
                LD_LIBRARY_PATH=\\\$(pwd)/lib make VERBOSE=1
            \""""
        }

        stage('Test') {
            def test_output = "TestResults.xml"
            sh """docker exec ${container_name} ${custom_sh} -c \"
                cd build
                ./bin/tests -- --gtest_output=xml:${test_output}
            \""""

            // Remove file outside container.
            sh "rm -f ${test_output}"
            // Copy and publish test results.
            sh "docker cp ${container_name}:/home/jenkins/build/${test_output} ."

            junit "${test_output}"
        }

        stage('Archive') {
            def archive_output = "forward-epics-to-kafka-centos.tar.gz"
            sh """docker exec ${container_name} ${custom_sh} -c \"
                cd build
                rm -rf forward-epics-to-kafka; mkdir forward-epics-to-kafka
                mkdir forward-epics-to-kafka/bin
                cp ./bin/forward-epics-to-kafka forward-epics-to-kafka/bin/
                cp -r ./lib forward-epics-to-kafka/
                cp -r ./licenses forward-epics-to-kafka/
                tar czf ${archive_output} forward-epics-to-kafka
            \""""
            sh "docker cp \
                ${container_name}:/home/jenkins/build/${archive_output} ."
            archiveArtifacts "${archive_output}"
        }
    } catch (e) {
        failure_function(e, 'Failed to build.')
    } finally {
        container.stop()
    }
}
